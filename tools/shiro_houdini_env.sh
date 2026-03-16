#!/usr/bin/env bash

set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(cd -- "${script_dir}/.." && pwd)

build_dir=${SHIRO_BUILD_DIR:-"${repo_root}/build-linux-houdini20.5-cuda"}
if [[ ! -d "${build_dir}/stage" ]]; then
    build_dir="${repo_root}/build-linux-houdini20.5"
fi

stage_dir="${build_dir}/stage"
plugin_dir="${stage_dir}/dso/usd_plugins"

if [[ ! -d "${plugin_dir}" ]]; then
    printf 'shiro_houdini_env: plugin dir not found: %s\n' "${plugin_dir}" >&2
    exit 1
fi

export PYTHONNOUSERSITE=1
export HOUDINI_PATH="${stage_dir};&"
export PXR_PLUGINPATH_NAME="${plugin_dir}"

if [[ $# -eq 0 ]]; then
    printf 'export PYTHONNOUSERSITE=1\n'
    printf 'export HOUDINI_PATH=%q\n' "${HOUDINI_PATH}"
    printf 'export PXR_PLUGINPATH_NAME=%q\n' "${PXR_PLUGINPATH_NAME}"
    exit 0
fi

exec "$@"
