#!/usr/bin/env bash
set -euo pipefail
repo=${1:?usage: rollback.sh /absolute/path/to/SesameV3_语音机器人项目}
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
patch -d "$repo" -p1 -R < "$script_dir/servo-r3-l4-inversion.patch"
