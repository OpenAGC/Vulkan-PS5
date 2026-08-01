#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
build_dir=${VULKAN_PS5_PROSPERO_BUILD:-"$script_dir/../build-prospero-m2"}

VULKAN_PS5_EXIT_ELF="$build_dir/vulkan_ps5_large_points_probe.elf" \
VULKAN_PS5_EXIT_REMOTE_NAME=vulkan_ps5_large_points \
VULKAN_PS5_EXIT_FILE_STEM=large-points-run1 \
VULKAN_PS5_EXIT_DISPLAY_NAME='large-points probe' \
VULKAN_PS5_EXIT_SUCCESS_REGEX='^large_points: PASS size8=64 size16=256 size32=1024 center=ff0000ff$' \
VULKAN_PS5_EXIT_FAILURE_PATTERN='large_points: mismatch' \
    exec "$script_dir/run_fw550_system_exit_probe.sh"
