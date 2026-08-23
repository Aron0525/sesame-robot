#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
template_root="$(cd "${script_dir}/../workspaces" && pwd)"
sesame_openclaw_home="${OPENCLAW_HOME:-$HOME/.openclaw}"

mkdir -p "${sesame_openclaw_home}"

for agent_id in sesame sesame-learning sesame-children sesame-work; do
  source_dir="${template_root}/${agent_id}"
  target_dir="${sesame_openclaw_home}/workspace-${agent_id}"

  if [[ ! -d "${source_dir}" ]]; then
    echo "Missing workspace template: ${source_dir}" >&2
    exit 1
  fi

  if [[ -e "${target_dir}" ]]; then
    echo "Refusing to overwrite existing workspace: ${target_dir}" >&2
    exit 1
  fi

  mkdir -p "${target_dir}"
  cp -R "${template_root}/shared/." "${target_dir}/"
  cp -R "${source_dir}/." "${target_dir}/"
  : > "${target_dir}/USER.md"
  : > "${target_dir}/MEMORY.md"
  echo "Installed ${agent_id} workspace at ${target_dir}"
done

echo "Workspace templates installed. Add or verify the four agents, then merge ops/openclaw/config/agents.template.json manually."
