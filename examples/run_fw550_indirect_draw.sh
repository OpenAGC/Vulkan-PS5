#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(dirname -- "$script_dir")
build_dir=${VULKAN_PS5_PROSPERO_BUILD:-$repo_dir/build-prospero-m2}

VULKAN_PS5_EXIT_ELF="$build_dir/vulkan_ps5_indirect_draw_probe.elf" \
VULKAN_PS5_EXIT_REMOTE_NAME=vulkan_ps5_indirect_draw \
VULKAN_PS5_EXIT_FILE_STEM=indirect-draw-run1 \
VULKAN_PS5_EXIT_DISPLAY_NAME='indirect-draw probe' \
VULKAN_PS5_EXIT_SUCCESS_REGEX='^indirect_draw: PASS green=[0-9]+ blue=[0-9]+ firstVertex=1 firstInstance=1,2 drawID=0,1 draws=2$' \
VULKAN_PS5_EXIT_FAILURE_PATTERN='indirect_draw: mismatch' \
    exec "$script_dir/run_fw550_system_exit_probe.sh"
