#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
build_dir=${VULKAN_PS5_PROSPERO_BUILD:-"$script_dir/../build-prospero-m2"}

VULKAN_PS5_EXIT_ELF="$build_dir/vulkan_ps5_vertex_pipeline_stores_atomics_probe.elf" \
VULKAN_PS5_EXIT_REMOTE_NAME=vulkan_ps5_vertex_pipeline_stores_atomics \
VULKAN_PS5_EXIT_FILE_STEM=vertex-pipeline-stores-atomics-run1 \
VULKAN_PS5_EXIT_DISPLAY_NAME='vertex-pipeline-stores-atomics probe' \
VULKAN_PS5_EXIT_SUCCESS_REGEX='^vertex_pipeline_stores_atomics: PASS green=7200 stages=VS,TCS,TES,GS atomic=4 stores=4$' \
VULKAN_PS5_EXIT_FAILURE_PATTERN='vertex_pipeline_stores_atomics: mismatch' \
    exec "$script_dir/run_fw550_system_exit_probe.sh"
