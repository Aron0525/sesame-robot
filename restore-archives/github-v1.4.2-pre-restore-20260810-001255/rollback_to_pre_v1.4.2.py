#!/usr/bin/env python3
from pathlib import Path
import json, shutil, subprocess
archive=Path('/Users/mac/Documents/sesame robot/restore-archives/github-v1.4.2-pre-restore-20260810-001255')
root=Path('/Users/mac/Desktop/2')
info=json.loads((archive/"manifest.json").read_text())
subprocess.run(["git","-C",str(root),"reset","--hard",info["pre_head"]],check=True)
for rel in (archive/"target-added-paths.txt").read_text().splitlines():
    target=root/rel
    if target.is_file() or target.is_symlink():
        target.unlink()
for src in (archive/"untracked-before").rglob("*"):
    if src.is_file():
        dst=root/src.relative_to(archive/"untracked-before")
        dst.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(src,dst)
subprocess.run(["git","-C",str(root),"apply","--binary","--whitespace=nowarn",str(archive/"tracked-before.patch")],check=True)
print("pre-v1.4.2 code state restored")
