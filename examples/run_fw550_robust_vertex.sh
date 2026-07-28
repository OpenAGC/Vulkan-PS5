#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
build_dir=${VULKAN_PS5_PROSPERO_BUILD:-"$script_dir/../build-prospero-m2"}

VULKAN_PS5_EXIT_ELF="$build_dir/vulkan_ps5_robust_vertex_probe.elf" \
VULKAN_PS5_EXIT_REMOTE_NAME=vulkan_ps5_robust_vertex \
VULKAN_PS5_EXIT_FILE_STEM=robust-vertex-run1 \
VULKAN_PS5_EXIT_DISPLAY_NAME='robust-vertex-access probe' \
VULKAN_PS5_EXIT_SUCCESS_REGEX='^robust_vertex_access: PASS OOB attribute=0 blue=[0-9]+$' \
VULKAN_PS5_EXIT_FAILURE_PATTERN='robust_vertex_access: mismatch' \
    exec "$script_dir/run_fw550_system_exit_probe.sh"
