#!/usr/bin/env python3
"""Train an ESP32-compatible int8 wake-word model from the five-voice corpus."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import random
import wave
from pathlib import Path
from typing import Iterable

os.environ.setdefault("TF_CPP_MIN_LOG_LEVEL", "2")

import numpy as np
import tensorflow as tf


SAMPLE_RATE_HZ = 16_000
CLIP_SAMPLES = SAMPLE_RATE_HZ
FRAME_SIZE = 480
FRAME_COUNT = CLIP_SAMPLES // FRAME_SIZE
DFT_BINS = np.rint(np.linspace(6, 120, 32)).astype(np.int32)
SEED = 20260810


def model_input_features(clip: np.ndarray) -> np.ndarray:
    """Exact log-power DFT feature extraction used by wake_vad_engine.cpp."""
    if clip.shape != (CLIP_SAMPLES,):
        raise ValueError(f"expected {CLIP_SAMPLES} mono samples, got {clip.shape}")
    frames = clip[: FRAME_COUNT * FRAME_SIZE].reshape(FRAME_COUNT, FRAME_SIZE)
    window = np.hanning(FRAME_SIZE).astype(np.float32)
    spectrum = np.fft.rfft(frames * window[None, :], axis=1)
    power = (spectrum.real * spectrum.real + spectrum.imag * spectrum.imag) / float(FRAME_SIZE * FRAME_SIZE)
    return np.log(power[:, DFT_BINS] + 1e-9).astype(np.float32)


def load_wav(path: Path) -> np.ndarray:
    with wave.open(str(path), "rb") as file:
        if file.getnchannels() != 1 or file.getsampwidth() != 2 or file.getframerate() != SAMPLE_RATE_HZ:
            raise ValueError(f"invalid 16 kHz mono PCM S16LE source: {path}")
        samples = np.frombuffer(file.readframes(file.getnframes()), dtype="<i2").astype(np.float32) / 32768.0
    non_silent = np.flatnonzero(np.abs(samples) > 0.004)
    if len(non_silent):
        samples = samples[max(0, non_silent[0] - 320) : min(len(samples), non_silent[-1] + 321)]
    return samples


def resample(audio: np.ndarray, speed: float) -> np.ndarray:
    if speed <= 0:
        raise ValueError("speed must be positive")
    output_length = max(1, int(round(len(audio) / speed)))
    positions = np.linspace(0, len(audio) - 1, output_length, dtype=np.float32)
    return np.interp(positions, np.arange(len(audio), dtype=np.float32), audio).astype(np.float32)


def make_clip(source: np.ndarray, rng: np.random.Generator, augment: bool) -> np.ndarray:
    speed = float(rng.uniform(0.84, 1.18)) if augment else 1.0
    speech = resample(source, speed)
    max_speech = int(SAMPLE_RATE_HZ * 0.78)
    if len(speech) > max_speech:
        speech = resample(speech, len(speech) / max_speech)
    if augment:
        speech *= float(10.0 ** (rng.uniform(-16.0, 6.0) / 20.0))
    clip = np.zeros(CLIP_SAMPLES, dtype=np.float32)
    minimum_start = int(SAMPLE_RATE_HZ * 0.04)
    maximum_start = CLIP_SAMPLES - len(speech) - int(SAMPLE_RATE_HZ * 0.06)
    start = minimum_start if maximum_start <= minimum_start else int(rng.integers(minimum_start, maximum_start + 1))
    clip[start : start + len(speech)] = speech
    if augment:
        speech_rms = float(np.sqrt(np.mean(speech * speech)) + 1e-6)
        clip = add_environment(clip, speech_rms, rng)
    return np.clip(clip, -1.0, 1.0).astype(np.float32)


def add_environment(clip: np.ndarray, speech_rms: float, rng: np.random.Generator) -> np.ndarray:
    for _ in range(int(rng.integers(1, 4))):
        delay = int(rng.integers(192, 1248))
        gain = float(rng.uniform(-0.38, 0.48))
        clip[delay:] += clip[:-delay] * gain
    snr_db = float(rng.uniform(8.0, 38.0))
    noise_rms = speech_rms / (10.0 ** (snr_db / 20.0))
    white = rng.normal(0.0, noise_rms, CLIP_SAMPLES).astype(np.float32)
    pink = np.cumsum(rng.normal(0.0, noise_rms * 0.018, CLIP_SAMPLES)).astype(np.float32)
    pink -= np.mean(pink)
    pink *= noise_rms / (np.sqrt(np.mean(pink * pink)) + 1e-8)
    hum = noise_rms * float(rng.uniform(0.08, 0.35)) * np.sin(
        2 * np.pi * float(rng.choice([50, 60, 100, 120])) * np.arange(CLIP_SAMPLES) / SAMPLE_RATE_HZ
        + float(rng.uniform(0, 2 * np.pi))
    )
    return clip + 0.45 * white + 0.45 * pink + hum.astype(np.float32)


def examples(records: list[dict[str, object]], root: Path, rng: np.random.Generator, copies: int, augment: bool) -> tuple[np.ndarray, np.ndarray]:
    values: list[np.ndarray] = []
    labels: list[int] = []
    for record in records:
        source = load_wav(root / str(record["path"]))
        for _ in range(copies):
            values.append(model_input_features(make_clip(source, rng, augment)))
            labels.append(int(record["label"]))
    if augment:
        for _ in range(max(1, len(values) // 3)):
            noise = rng.normal(0.0, float(10.0 ** (rng.uniform(-58, -30) / 20.0)), CLIP_SAMPLES).astype(np.float32)
            values.append(model_input_features(noise))
            labels.append(0)
    return np.stack(values)[..., None], np.asarray(labels, dtype=np.int32)


def build_model() -> tf.keras.Model:
    inputs = tf.keras.Input(shape=(FRAME_COUNT, len(DFT_BINS), 1), name="log_power")
    x = tf.keras.layers.Conv2D(10, 3, padding="same", activation="relu")(inputs)
    x = tf.keras.layers.MaxPooling2D(pool_size=(2, 2))(x)
    x = tf.keras.layers.Conv2D(14, 3, padding="same", activation="relu")(x)
    x = tf.keras.layers.MaxPooling2D(pool_size=(2, 2))(x)
    x = tf.keras.layers.Flatten()(x)
    x = tf.keras.layers.Dense(18, activation="relu")(x)
    x = tf.keras.layers.Dropout(0.18)(x)
    outputs = tf.keras.layers.Dense(2, activation="softmax", name="class_probability")(x)
    return tf.keras.Model(inputs, outputs, name="zhima_five_tts_dscnn")


def tflite_scores(interpreter: tf.lite.Interpreter, features: np.ndarray) -> np.ndarray:
    input_detail = interpreter.get_input_details()[0]
    output_detail = interpreter.get_output_details()[0]
    input_scale, input_zero_point = input_detail["quantization"]
    output_scale, output_zero_point = output_detail["quantization"]
    scores: list[float] = []
    for feature in features:
        quantized = np.clip(np.rint(feature / input_scale + input_zero_point), -128, 127).astype(np.int8)[None, ...]
        interpreter.set_tensor(input_detail["index"], quantized)
        interpreter.invoke()
        raw = interpreter.get_tensor(output_detail["index"])[0, 1]
        scores.append(float((int(raw) - output_zero_point) * output_scale))
    return np.asarray(scores, dtype=np.float32)


def select_threshold(scores: np.ndarray, labels: np.ndarray) -> tuple[float, dict[str, int | float | bool]]:
    candidates: list[tuple[float, dict[str, int | float | bool]]] = []
    for threshold in np.arange(0.30, 0.991, 0.01):
        predicted = scores >= threshold
        positive = labels == 1
        true_positive = int(np.sum(predicted & positive))
        false_positive = int(np.sum(predicted & ~positive))
        false_negative = int(np.sum(~predicted & positive))
        row: dict[str, int | float | bool] = {
            "threshold": round(float(threshold), 2),
            "tp": true_positive,
            "fn": false_negative,
            "fp": false_positive,
            "tn": int(np.sum(~predicted & ~positive)),
            "recall": true_positive / max(1, int(np.sum(positive))),
        }
        candidates.append((float(threshold), row))
    accepted = [(threshold, row) for threshold, row in candidates if row["recall"] >= 1.0 and row["fp"] == 0]
    if not accepted:
        threshold, row = min(candidates, key=lambda item: (int(item[1]["fp"]), -float(item[1]["recall"])))
        row["accepted"] = False
        return threshold, row
    threshold, row = accepted[-1]
    row["accepted"] = True
    return threshold, row


def convert_to_int8(network: tf.keras.Model, training_features: np.ndarray) -> bytes:
    converter = tf.lite.TFLiteConverter.from_keras_model(network)
    converter.optimizations = [tf.lite.Optimize.DEFAULT]

    def representative_dataset() -> Iterable[list[np.ndarray]]:
        for feature in training_features[::8]:
            yield [feature[None, ...].astype(np.float32)]

    converter.representative_dataset = representative_dataset
    converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
    converter.inference_input_type = tf.int8
    converter.inference_output_type = tf.int8
    return converter.convert()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--corpus", required=True, type=Path, help="directory containing manifest.json and wav/")
    parser.add_argument("--output", required=True, type=Path, help="directory for model and metrics")
    arguments = parser.parse_args()
    corpus_root = arguments.corpus.resolve()
    output_root = arguments.output.resolve()
    output_root.mkdir(parents=True, exist_ok=True)

    random.seed(SEED)
    np.random.seed(SEED)
    tf.keras.utils.set_random_seed(SEED)
    tf.config.threading.set_inter_op_parallelism_threads(1)
    tf.config.threading.set_intra_op_parallelism_threads(1)
    rng = np.random.default_rng(SEED)

    manifest = json.loads((corpus_root / "manifest.json").read_text(encoding="utf-8"))
    train_records = [record for record in manifest["records"] if record["split"] == "train"]
    holdout_records = [record for record in manifest["records"] if record["split"] == "holdout"]
    if not train_records or not holdout_records:
        raise RuntimeError("corpus must include non-empty train and holdout voice groups")

    x_train, y_train = examples(train_records, corpus_root, rng, copies=36, augment=True)
    x_holdout_augmented, y_holdout_augmented = examples(holdout_records, corpus_root, rng, copies=18, augment=True)
    x_holdout, y_holdout = examples(holdout_records, corpus_root, rng, copies=1, augment=False)
    mean = float(np.mean(x_train))
    std = float(np.std(x_train) + 1e-6)
    normalize = lambda value: np.clip((value - mean) / std, -6.0, 6.0).astype(np.float32)
    x_train, x_holdout_augmented, x_holdout = map(normalize, (x_train, x_holdout_augmented, x_holdout))

    network = build_model()
    network.compile(optimizer=tf.keras.optimizers.Adam(learning_rate=0.0012), loss="sparse_categorical_crossentropy", metrics=["accuracy"])
    positives = int(np.sum(y_train == 1))
    negatives = int(np.sum(y_train == 0))
    history = network.fit(
        x_train, y_train,
        validation_data=(x_holdout_augmented, y_holdout_augmented),
        epochs=72,
        batch_size=64,
        verbose=2,
        class_weight={0: 1.0, 1: negatives / max(1, positives)},
        callbacks=[tf.keras.callbacks.EarlyStopping(monitor="val_loss", patience=12, restore_best_weights=True)],
    )
    model_bytes = convert_to_int8(network, x_train)
    model_path = output_root / "zhima_wakeword_five_tts_int8.tflite"
    model_path.write_bytes(model_bytes)

    interpreter = tf.lite.Interpreter(model_path=str(model_path))
    interpreter.allocate_tensors()
    scores = tflite_scores(interpreter, x_holdout)
    threshold, holdout = select_threshold(scores, y_holdout)
    input_detail = interpreter.get_input_details()[0]
    output_detail = interpreter.get_output_details()[0]
    operations = [operation["op_name"] for operation in interpreter._get_ops_details()]
    metrics = {
        "model_name": "zhima_wakeword_five_tts_int8",
        "target_wake_word": "你好，芝麻",
        "source": {"selected_tts_voices": [record["voice"] for record in train_records[::8]] + [holdout_records[0]["voice"]], "train_records": len(train_records), "holdout_records": len(holdout_records)},
        "feature_contract": {"sample_rate_hz": SAMPLE_RATE_HZ, "clip_samples": CLIP_SAMPLES, "frame_size": FRAME_SIZE, "frame_count": FRAME_COUNT, "frequency_dft_bins": DFT_BINS.tolist(), "normalization_mean": mean, "normalization_std": std, "clip_range": [-6.0, 6.0]},
        "training": {"seed": SEED, "epochs_run": len(history.history["loss"]), "train_examples": len(x_train), "holdout_augmented_examples": len(x_holdout_augmented)},
        "tflite": {"path": str(model_path), "bytes": len(model_bytes), "sha256": hashlib.sha256(model_bytes).hexdigest(), "input": {"shape": input_detail["shape"].tolist(), "dtype": str(input_detail["dtype"]), "scale": float(input_detail["quantization"][0]), "zero_point": int(input_detail["quantization"][1])}, "output": {"shape": output_detail["shape"].tolist(), "dtype": str(output_detail["dtype"]), "operators": operations}},
        "threshold": threshold,
        "holdout": {**holdout, "rows": [{"id": record["id"], "label": int(label), "score": float(score)} for record, label, score in zip(holdout_records, y_holdout, scores)]},
        "deployment_gate": bool(holdout["accepted"] and input_detail["dtype"] == np.int8 and output_detail["dtype"] == np.int8 and set(operations).issubset({"CONV_2D", "MAX_POOL_2D", "RESHAPE", "FULLY_CONNECTED", "SOFTMAX", "DELEGATE"})),
    }
    (output_root / "training_metrics.json").write_text(json.dumps(metrics, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"model": str(model_path), "holdout": metrics["holdout"], "deployment_gate": metrics["deployment_gate"]}, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
