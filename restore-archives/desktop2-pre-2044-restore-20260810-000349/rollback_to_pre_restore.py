#!/usr/bin/env python3
from pathlib import Path
import shutil
root=Path('/Users/mac/Desktop/2')
archive=Path('/Users/mac/Documents/sesame robot/restore-archives/desktop2-pre-2044-restore-20260810-000349')
manifest=__import__("json").loads((archive/"restore-manifest.json").read_text())
for item in manifest["restored"]:
    src=archive/"current-before-restore"/item["path"]
    dst=root/item["path"]
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src,dst)
for relative in manifest["removed_new_source"]:
    src=archive/"moved-from-workspace/new-source"/relative
    dst=root/relative
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.move(str(src),str(dst))
for relative in manifest["moved_generated"]:
    src=archive/"moved-from-workspace/generated"/relative
    dst=root/relative
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.move(str(src),str(dst))
recordings=root/"gateway/test-recordings"
recordings.mkdir(parents=True,exist_ok=True)
for src in (archive/"moved-from-workspace/recordings").glob("*.wav"):
    shutil.move(str(src),str(recordings/src.name))
manifest_src=archive/"moved-from-workspace/recordings/manifest.jsonl"
if manifest_src.exists():
    shutil.copy2(manifest_src,recordings/"manifest.jsonl")
print("rollback to pre-restore state complete")
