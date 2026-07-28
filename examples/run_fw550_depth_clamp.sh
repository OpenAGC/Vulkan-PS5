#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(dirname -- "$script_dir")
build_dir=${VULKAN_PS5_PROSPERO_BUILD:-$repo_dir/build-prospero-m2}

VULKAN_PS5_EXIT_ELF="$build_dir/vulkan_ps5_depth_clamp_probe.elf" \
VULKAN_PS5_EXIT_REMOTE_NAME=vulkan_ps5_depth_clamp \
VULKAN_PS5_EXIT_FILE_STEM=depth-clamp-run1 \
VULKAN_PS5_EXIT_DISPLAY_NAME='depth-clamp probe' \
VULKAN_PS5_EXIT_SUCCESS_REGEX='^depth_clamp: PASS green=[0-9]+ red=[0-9]+ raw=[0-9]+/[0-9]+/[0-9]+ stencil=[0-9]+$' \
VULKAN_PS5_EXIT_FAILURE_PATTERN='depth_clamp: mismatch' \
    exec "$script_dir/run_fw550_system_exit_probe.sh"
