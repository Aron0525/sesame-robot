#!/usr/bin/env python3
"""Fresh TTS-only int8 wake-word training for 你好芝麻 on ESP32-S3.

The model input is the exact 33 x 32 log-power DFT feature matrix implemented
in firmware/main/main.cpp.  All outputs remain isolated in artifacts/tts_v2.
"""
from __future__ import annotations

import hashlib
import json
import os
import random
from collections import Counter
from pathlib import Path
from typing import Iterable

os.environ.setdefault("TF_CPP_MIN_LOG_LEVEL", "2")

import numpy as np
import soundfile as sf
import tensorflow as tf
from scipy.signal import butter, resample_poly, sosfilt

ROOT = Path(os.environ["WAKEWORD_SOURCE_ROOT"]).resolve()
OUT = Path(os.environ["WAKEWORD_OUTPUT_DIR"]).resolve()
MANIFEST_PATH = OUT / "manifest.json"
FINAL_HOLDOUT_PATH = ROOT / "artifacts" / "final_holdout_tts_manifest.json"
SAMPLE_RATE = 16_000
CLIP_SAMPLES = SAMPLE_RATE
FRAME_SIZE = 480
FRAME_COUNT = CLIP_SAMPLES // FRAME_SIZE
FREQUENCY_BINS = np.rint(np.linspace(6, 120, 32)).astype(np.int32)
SEED = 20260807
VALIDATION_GROUPS = {"edge:zh-CN-YunjianNeural", "edge:zh-CN-YunxiaNeural", "mac:Sinji", "gtts:com.hk"}


def set_seed() -> np.random.Generator:
    random.seed(SEED)
    np.random.seed(SEED)
    tf.keras.utils.set_random_seed(SEED)
    return np.random.default_rng(SEED)


def load_wav(relative_path: str) -> np.ndarray:
    data, sr = sf.read(ROOT / relative_path, dtype="float32", always_2d=False)
    if sr != SAMPLE_RATE or data.ndim != 1:
        raise ValueError(f"invalid PCM contract: {relative_path}: sr={sr}, shape={data.shape}")
    # TTS lead/trail silence has no phonetic information and would make placement
    # augmentation ineffective.  Keep a 20 ms boundary so plosives are retained.
    non_silent = np.flatnonzero(np.abs(data) > 0.004)
    if len(non_silent):
        data = data[max(0, non_silent[0] - 320):min(len(data), non_silent[-1] + 321)]
    return np.asarray(data, dtype=np.float32)


def rate_resample(audio: np.ndarray, speed: float) -> np.ndarray:
    return resample_poly(audio, 1000, max(1, int(round(1000 * speed)))).astype(np.float32)


def fit_spoken_region(audio: np.ndarray, speed: float) -> np.ndarray:
    """Fit an utterance inside a 1 s wakeword window without truncating its end."""
    audio = rate_resample(audio, speed)
    max_speech = int(SAMPLE_RATE * 0.78)
    if len(audio) > max_speech:
        audio = rate_resample(audio, len(audio) / max_speech)
    return audio


def add_room_and_noise(clip: np.ndarray, speech_rms: float, rng: np.random.Generator) -> np.ndarray:
    # Compact random room impulse response: 12--78 ms echoes with decay.
    if rng.random() < 0.8:
        taps = np.zeros(int(SAMPLE_RATE * 0.09), dtype=np.float32)
        taps[0] = 1.0
        for _ in range(int(rng.integers(1, 4))):
            delay = int(rng.integers(192, 1248))
            taps[delay] += float(rng.uniform(-0.38, 0.48))
        clip = np.convolve(clip, taps, mode="full")[:CLIP_SAMPLES]
    # Random small-speaker/microphone frequency response.
    if rng.random() < 0.75:
        cutoff = float(rng.uniform(3500, 7200))
        clip = sosfilt(butter(2, cutoff, fs=SAMPLE_RATE, btype="low", output="sos"), clip).astype(np.float32)
    snr_db = float(rng.uniform(8.0, 38.0))
    noise_rms = speech_rms / (10.0 ** (snr_db / 20.0))
    white = rng.normal(0.0, noise_rms, CLIP_SAMPLES).astype(np.float32)
    pink = np.cumsum(rng.normal(0.0, noise_rms * 0.018, CLIP_SAMPLES)).astype(np.float32)
    pink -= np.mean(pink)
    pink *= noise_rms / (np.sqrt(np.mean(pink * pink)) + 1e-8)
    phase = float(rng.uniform(0, 2 * np.pi))
    hum = (noise_rms * rng.uniform(0.08, 0.35) * np.sin(
        2 * np.pi * float(rng.choice([50, 60, 100, 120])) * np.arange(CLIP_SAMPLES) / SAMPLE_RATE + phase
    )).astype(np.float32)
    return clip + 0.45 * white + 0.45 * pink + hum


def clip_from_source(source: np.ndarray, rng: np.random.Generator, augment: bool) -> np.ndarray:
    speed = float(rng.uniform(0.84, 1.18)) if augment else 1.0
    audio = fit_spoken_region(source, speed)
    if augment:
        audio *= float(10.0 ** (rng.uniform(-16.0, 6.0) / 20.0))
    clip = np.zeros(CLIP_SAMPLES, dtype=np.float32)
    min_start = int(SAMPLE_RATE * 0.04)
    max_start = CLIP_SAMPLES - len(audio) - int(SAMPLE_RATE * 0.06)
    start = min_start if max_start <= min_start else int(rng.integers(min_start, max_start + 1))
    clip[start:start + len(audio)] = audio
    if augment:
        speech_rms = float(np.sqrt(np.mean(audio * audio)) + 1e-6)
        clip = add_room_and_noise(clip, speech_rms, rng)
    return np.clip(clip, -1.0, 1.0).astype(np.float32)


def silence_clip(rng: np.random.Generator) -> np.ndarray:
    rms = float(10.0 ** (rng.uniform(-58.0, -30.0) / 20.0))
    clip = rng.normal(0.0, rms, CLIP_SAMPLES).astype(np.float32)
    if rng.random() < 0.75:
        clip = add_room_and_noise(clip, max(rms, 1e-5), rng)
    return np.clip(clip, -1.0, 1.0).astype(np.float32)


def features(clip: np.ndarray) -> np.ndarray:
    if clip.shape != (CLIP_SAMPLES,):
        raise ValueError(f"expected 16000 samples, got {clip.shape}")
    frames = clip[:FRAME_COUNT * FRAME_SIZE].reshape(FRAME_COUNT, FRAME_SIZE)
    window = np.hanning(FRAME_SIZE).astype(np.float32)
    spectrum = np.fft.rfft(frames * window[None, :], axis=1)
    power = (spectrum.real ** 2 + spectrum.imag ** 2) / float(FRAME_SIZE * FRAME_SIZE)
    return np.log(power[:, FREQUENCY_BINS] + 1e-9).astype(np.float32)


def records_for_groups(records: list[dict], groups: set[str]) -> list[dict]:
    return [r for r in records if str(r["split_group"]) in groups]


def make_examples(records: list[dict], rng: np.random.Generator, augment_count: int) -> tuple[np.ndarray, np.ndarray, list[str]]:
    x: list[np.ndarray] = []
    y: list[int] = []
    ids: list[str] = []
    for record in records:
        source = load_wav(str(record["path"]))
        for _ in range(augment_count):
            x.append(features(clip_from_source(source, rng, augment=True)))
            y.append(int(record["label"]))
            ids.append(str(record["id"]))
    # Silence and background-only segments are independent examples rather than
    # copies from an utterance, so they cannot leak between speaker splits.
    for _ in range(max(1, len(x) // 3)):
        x.append(features(silence_clip(rng)))
        y.append(0)
        ids.append("synthetic_background")
    return np.stack(x)[..., None], np.asarray(y, dtype=np.int32), ids


def clean_examples(records: list[dict]) -> tuple[np.ndarray, np.ndarray, list[str], list[str]]:
    rng = np.random.default_rng(SEED + 141)
    x: list[np.ndarray] = []
    y: list[int] = []
    ids: list[str] = []
    categories: list[str] = []
    for record in records:
        x.append(features(clip_from_source(load_wav(str(record["path"])), rng, augment=False)))
        y.append(int(record["label"]))
        ids.append(str(record["id"]))
        categories.append(str(record["category"]))
    return np.stack(x)[..., None], np.asarray(y, dtype=np.int32), ids, categories


def model() -> tf.keras.Model:
    inputs = tf.keras.Input(shape=(FRAME_COUNT, len(FREQUENCY_BINS), 1), name="log_power")
    x = tf.keras.layers.Conv2D(10, 3, padding="same", activation="relu")(inputs)
    x = tf.keras.layers.MaxPooling2D(pool_size=(2, 2))(x)
    x = tf.keras.layers.Conv2D(14, 3, padding="same", activation="relu")(x)
    x = tf.keras.layers.MaxPooling2D(pool_size=(2, 2))(x)
    x = tf.keras.layers.Flatten()(x)
    x = tf.keras.layers.Dense(18, activation="relu")(x)
    x = tf.keras.layers.Dropout(0.18)(x)
    outputs = tf.keras.layers.Dense(2, activation="softmax", name="class_probability")(x)
    return tf.keras.Model(inputs, outputs, name="zhima_tts_v2_dscnn")


def tflite_scores(interpreter: tf.lite.Interpreter, x: np.ndarray) -> np.ndarray:
    inp = interpreter.get_input_details()[0]
    out = interpreter.get_output_details()[0]
    scale, zero = inp["quantization"]
    result: list[float] = []
    for value in x:
        q = np.clip(np.rint(value / scale + zero), -128, 127).astype(np.int8)[None, ...]
        interpreter.set_tensor(inp["index"], q)
        interpreter.invoke()
        raw = interpreter.get_tensor(out["index"])[0].astype(np.float32)
        result.append(float((raw[1] - out["quantization"][1]) * out["quantization"][0]))
    return np.asarray(result, dtype=np.float32)


def threshold_row(scores: np.ndarray, labels: np.ndarray, threshold: float) -> dict[str, float | int]:
    pred = scores >= threshold
    positives = labels == 1
    tp = int(np.sum(pred & positives)); fn = int(np.sum(~pred & positives))
    fp = int(np.sum(pred & ~positives)); tn = int(np.sum(~pred & ~positives))
    return {"threshold": round(float(threshold), 3), "tp": tp, "fn": fn, "fp": fp, "tn": tn,
            "recall": float(tp / max(1, tp + fn)),
            "false_accept_rate": float(fp / max(1, fp + tn)),
            "accuracy": float((tp + tn) / max(1, len(labels)))}


def select_threshold(scores: np.ndarray, labels: np.ndarray) -> tuple[float, list[dict[str, float | int]]]:
    rows = [threshold_row(scores, labels, threshold) for threshold in np.arange(0.40, 0.991, 0.01)]
    valid = [r for r in rows if r["recall"] >= 0.95 and r["fp"] == 0]
    if valid:
        return float(valid[0]["threshold"]), rows
    # A deterministic conservative fallback if the validation partition lacks a
    # zero-FP operating point.  This condition is promoted into deployment_gate.
    candidates = sorted(rows, key=lambda r: (-float(r["accuracy"]), -float(r["recall"]), int(r["fp"])))
    return float(candidates[0]["threshold"]), rows


def build_external_records() -> list[dict]:
    if not FINAL_HOLDOUT_PATH.exists():
        return []
    return json.loads(FINAL_HOLDOUT_PATH.read_text())["records"]


def category_summary(scores: np.ndarray, labels: np.ndarray, categories: list[str], threshold: float) -> dict[str, dict[str, float | int]]:
    result: dict[str, dict[str, float | int]] = {}
    for category in sorted(set(categories)):
        mask = np.asarray([x == category for x in categories])
        result[category] = {**threshold_row(scores[mask], labels[mask], threshold),
                            "score_min": float(np.min(scores[mask])), "score_max": float(np.max(scores[mask])),
                            "score_mean": float(np.mean(scores[mask]))}
    return result


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    rng = set_seed()
    manifest = json.loads(MANIFEST_PATH.read_text())
    records = manifest["records"]
    all_groups = {str(r["split_group"]) for r in records}
    train_groups = all_groups - VALIDATION_GROUPS
    train_records = records_for_groups(records, train_groups)
    val_records = records_for_groups(records, VALIDATION_GROUPS)
    if not train_records or not val_records:
        raise RuntimeError("speaker-disjoint split is empty")

    x_train, y_train, _ = make_examples(train_records, rng, augment_count=28)
    x_val, y_val, _ = make_examples(val_records, rng, augment_count=16)
    x_clean_val, y_clean_val, clean_ids, clean_categories = clean_examples(val_records)

    mean = float(np.mean(x_train)); std = float(np.std(x_train) + 1e-6)
    normalise = lambda x: np.clip((x - mean) / std, -6.0, 6.0).astype(np.float32)
    x_train, x_val, x_clean_val = normalise(x_train), normalise(x_val), normalise(x_clean_val)

    net = model()
    net.compile(optimizer=tf.keras.optimizers.Adam(learning_rate=0.0012),
                loss="sparse_categorical_crossentropy", metrics=["accuracy"])
    pos = int(np.sum(y_train == 1)); neg = int(np.sum(y_train == 0))
    weights = {0: 1.0, 1: float(neg / max(1, pos))}
    history = net.fit(x_train, y_train, validation_data=(x_val, y_val), epochs=72,
                      batch_size=64, verbose=2, class_weight=weights,
                      callbacks=[tf.keras.callbacks.EarlyStopping(monitor="val_loss", patience=12,
                                                                  restore_best_weights=True)])

    keras_path = OUT / "zhima_wakeword_dashscope10.keras"
    net.save(keras_path)
    converter = tf.lite.TFLiteConverter.from_keras_model(net)
    converter.optimizations = [tf.lite.Optimize.DEFAULT]
    def representative_dataset() -> Iterable[list[np.ndarray]]:
        for sample in x_train[::8]:
            yield [sample[None, ...].astype(np.float32)]
    converter.representative_dataset = representative_dataset
    converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
    converter.inference_input_type = tf.int8
    converter.inference_output_type = tf.int8
    model_bytes = converter.convert()
    model_path = OUT / "zhima_wakeword_dashscope10_int8.tflite"
    model_path.write_bytes(model_bytes)

    interpreter = tf.lite.Interpreter(model_path=str(model_path))
    interpreter.allocate_tensors()
    val_scores = tflite_scores(interpreter, x_clean_val)
    threshold, sweep = select_threshold(val_scores, y_clean_val)

    external_records = build_external_records()
    if external_records:
        x_ext, y_ext, ext_ids, ext_categories = clean_examples(external_records)
        ext_scores = tflite_scores(interpreter, normalise(x_ext))
        unseen_voice_indices = [i for i, r in enumerate(external_records) if str(r.get("engine", "")).startswith("Azure Edge")]
        unseen_voice_scores = ext_scores[unseen_voice_indices]
        unseen_voice_labels = y_ext[unseen_voice_indices]
        unseen_voice_categories = [ext_categories[i] for i in unseen_voice_indices]
    else:
        y_ext = np.empty(0, dtype=np.int32); ext_ids = []; ext_categories = []; ext_scores = np.empty(0, dtype=np.float32)
        unseen_voice_scores = np.empty(0, dtype=np.float32); unseen_voice_labels = np.empty(0, dtype=np.int32); unseen_voice_categories = []

    input_detail = interpreter.get_input_details()[0]
    output_detail = interpreter.get_output_details()[0]
    ops = [item["op_name"] for item in interpreter._get_ops_details()]
    val_stats = threshold_row(val_scores, y_clean_val, threshold)
    ext_stats = threshold_row(ext_scores, y_ext, threshold) if len(y_ext) else None
    hard_mask = np.asarray([c == "hard_negative_nihaoxiaozhi" for c in clean_categories])
    ext_hard_mask = np.asarray([c == "hard_negative_nihaoxiaozhi" for c in ext_categories])
    gates = {
        "speaker_disjoint_validation_zero_false_accepts": val_stats["fp"] == 0,
        "speaker_disjoint_validation_recall_at_least_0_95": val_stats["recall"] >= 0.95,
        "unseen_edge_voice_holdout_zero_false_accepts": bool(len(unseen_voice_labels) and threshold_row(unseen_voice_scores, unseen_voice_labels, threshold)["fp"] == 0),
        "unseen_edge_voice_holdout_recall_at_least_0_80": bool(len(unseen_voice_labels) and threshold_row(unseen_voice_scores, unseen_voice_labels, threshold)["recall"] >= 0.80),
        "int8_tensor_contract": bool(input_detail["dtype"] == np.int8 and output_detail["dtype"] == np.int8),
        "firmware_operator_set": set(ops).issubset({"CONV_2D", "MAX_POOL_2D", "RESHAPE", "FULLY_CONNECTED", "SOFTMAX", "DELEGATE"}),
    }
    deployment_gate = all(gates.values())
    predictions = {
        "speaker_disjoint_validation": [
            {"id": ident, "category": category, "label": int(label), "score": float(score)}
            for ident, category, label, score in zip(clean_ids, clean_categories, y_clean_val, val_scores)
        ],
        "independent_engine_holdout": [
            {"id": ident, "category": category, "label": int(label), "score": float(score)}
            for ident, category, label, score in zip(ext_ids, ext_categories, y_ext, ext_scores)
        ],
    }
    (OUT / "predictions.json").write_text(json.dumps(predictions, ensure_ascii=False, indent=2) + "\n")
    metrics = {
        "model_name": "zhima_wakeword_dashscope10_int8",
        "target_wake_word": "你好芝麻",
        "training_scope": "TTS-v2 corpus plus eight DashScope TTS positive recordings; two DashScope recordings are reserved for evaluation.",
        "architecture": "int8 DSCNN (Conv2D/MaxPool/FC) on 33x32 log-power DFT features",
        "feature_contract": {"sample_rate_hz": SAMPLE_RATE, "pcm": "signed 16-bit mono", "clip_samples": CLIP_SAMPLES,
            "frame_size": FRAME_SIZE, "frame_count": FRAME_COUNT, "frequency_dft_bins": FREQUENCY_BINS.tolist(),
            "normalization_mean": mean, "normalization_std": std, "clip_range": [-6.0, 6.0]},
        "split": {"method": "speaker/engine group disjoint", "training_groups": sorted(train_groups),
                  "validation_groups": sorted(VALIDATION_GROUPS), "source_records": {"train": len(train_records), "validation": len(val_records)}},
        "dataset": {"source_counts": dict(Counter(str(r["category"]) for r in records)),
                    "train_augmented_examples": int(len(x_train)), "validation_augmented_examples": int(len(x_val)),
                    "train_class_weight": weights, "augmentation": manifest["augmentation_contract"]},
        "training": {"seed": SEED, "epochs_run": len(history.history["loss"]),
                     "final_loss": float(history.history["loss"][-1]), "final_validation_loss": float(history.history["val_loss"][-1])},
        "tflite": {"path": str(model_path), "bytes": len(model_bytes), "sha256": hashlib.sha256(model_bytes).hexdigest(),
                   "input": {"shape": input_detail["shape"].tolist(), "dtype": str(input_detail["dtype"]), "scale": float(input_detail["quantization"][0]), "zero_point": int(input_detail["quantization"][1])},
                   "output": {"shape": output_detail["shape"].tolist(), "dtype": str(output_detail["dtype"]), "scale": float(output_detail["quantization"][0]), "zero_point": int(output_detail["quantization"][1])},
                   "operators": ops},
        "threshold_selection": {"threshold": threshold, "method": "lowest validation threshold with >=95% recall and zero false accepts", "speaker_disjoint_sweep": sweep},
        "speaker_disjoint_validation": {"overall": val_stats, "by_category": category_summary(val_scores, y_clean_val, clean_categories, threshold),
                                         "hard_negative_false_accepts": int(np.sum(val_scores[hard_mask] >= threshold))},
        "cross_synthesis_holdout": None if ext_stats is None else {"overall": ext_stats,
            "by_category": category_summary(ext_scores, y_ext, ext_categories, threshold),
            "hard_negative_false_accepts": int(np.sum(ext_scores[ext_hard_mask] >= threshold)),
            "manifest": str(FINAL_HOLDOUT_PATH.relative_to(ROOT)),
            "note": "gTTS appears in v2 training; the full set is cross-synthesis diagnostics, not an independent-engine gate."},
        "unseen_edge_voice_holdout": None if not len(unseen_voice_labels) else {"overall": threshold_row(unseen_voice_scores, unseen_voice_labels, threshold),
            "by_category": category_summary(unseen_voice_scores, unseen_voice_labels, unseen_voice_categories, threshold),
            "source": "Two Edge dialect voices excluded from v2 training"},
        "deployment_gate": {"passed": deployment_gate, "checks": gates},
        "limitation": "This is TTS-only validation. Deployment still requires microphone recordings from the target ESP32-S3 board before a production threshold is accepted.",
    }
    (OUT / "training_metrics.json").write_text(json.dumps(metrics, ensure_ascii=False, indent=2) + "\n")
    np.savez(OUT / "feature_stats.npz", mean=np.float32(mean), std=np.float32(std), bins=FREQUENCY_BINS)
    print(json.dumps({"model": str(model_path), "threshold": threshold, "validation": val_stats,
                      "external": ext_stats, "unseen_edge_voice": None if not len(unseen_voice_labels) else threshold_row(unseen_voice_scores, unseen_voice_labels, threshold), "deployment_gate": metrics["deployment_gate"]}, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
