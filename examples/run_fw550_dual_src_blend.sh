#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(dirname -- "$script_dir")
build_dir=${VULKAN_PS5_PROSPERO_BUILD:-$repo_dir/build-prospero-m2}

VULKAN_PS5_EXIT_ELF="$build_dir/vulkan_ps5_dual_src_blend_probe.elf" \
VULKAN_PS5_EXIT_REMOTE_NAME=vulkan_ps5_dual_src_blend \
VULKAN_PS5_EXIT_FILE_STEM=dual-src-blend-run1 \
VULKAN_PS5_EXIT_DISPLAY_NAME='dual-source blend probe' \
VULKAN_PS5_EXIT_SUCCESS_REGEX='^dual_src_blend: PASS covered=18432 center=ff00ff00 src1=green$' \
VULKAN_PS5_EXIT_FAILURE_PATTERN='dual_src_blend: mismatch' \
    exec "$script_dir/run_fw550_system_exit_probe.sh"
