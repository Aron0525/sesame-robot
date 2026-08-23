#!/usr/bin/env python3
"""Score DashScope train/holdout clips with the WakeNet int8 contract."""
from __future__ import annotations
import json, os, sys
from pathlib import Path
import numpy as np
import tensorflow as tf

source = Path(os.environ['WAKEWORD_SOURCE_ROOT']).resolve()
sys.path.insert(0, str(source / 'tools'))
import train_wakeword_tts_v2 as trainer  # noqa: E402

art = Path(os.environ['WAKEWORD_ARTIFACT_DIR']).resolve()
model_path = Path(os.environ['WAKEWORD_MODEL_PATH']).resolve()
metrics_path = Path(os.environ['WAKEWORD_METRICS_PATH']).resolve()
metrics = json.loads(metrics_path.read_text())
mean = float(metrics['feature_contract']['normalization_mean'])
std = float(metrics['feature_contract']['normalization_std'])
manifest = json.loads((art / 'tts/manifest.json').read_text())
interpreter = tf.lite.Interpreter(model_path=str(model_path)); interpreter.allocate_tensors()
rng = np.random.default_rng(20260809)
rows = []
for record in manifest['records']:
    audio = trainer.load_wav(record['path'])
    feature = trainer.features(trainer.clip_from_source(audio, rng, augment=False))[None, ..., None]
    feature = np.clip((feature - mean) / std, -6.0, 6.0).astype(np.float32)
    score = float(trainer.tflite_scores(interpreter, feature)[0])
    rows.append({'id':record['id'], 'split':record['split'], 'rate':record['rate'], 'duration_ms':record['format']['duration_ms'], 'score':score, 'trigger_at_0_85':score >= .85})
holdout=[x for x in rows if x['split']=='holdout']
result={
    'model':str(model_path),
    'model_sha256':__import__('hashlib').sha256(model_path.read_bytes()).hexdigest(),
    'threshold':0.85,
    'samples':rows,
    'holdout':{'count':len(holdout),'triggers':sum(x['trigger_at_0_85'] for x in holdout),'recall':sum(x['trigger_at_0_85'] for x in holdout)/len(holdout), 'passed':all(x['trigger_at_0_85'] for x in holdout)},
}
name=os.environ['WAKEWORD_EVALUATION_NAME']
(art/'verification'/name).write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n')
print(json.dumps(result,ensure_ascii=False,indent=2))
