#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
build_dir=${VULKAN_PS5_PROSPERO_BUILD:-"$script_dir/../build-prospero-m2"}

VULKAN_PS5_EXIT_ELF="$build_dir/vulkan_ps5_blit3d_probe.elf" \
VULKAN_PS5_EXIT_REMOTE_NAME=vulkan_ps5_blit3d \
VULKAN_PS5_EXIT_FILE_STEM=blit3d-run1 \
VULKAN_PS5_EXIT_DISPLAY_NAME='3D/self blit probe' \
VULKAN_PS5_REQUIRE_CLEANUP=1 \
VULKAN_PS5_EXIT_SUCCESS_REGEX='^blit3d: PASS volume=64 self=16$' \
VULKAN_PS5_EXIT_FAILURE_PATTERN='blit3d: .*mismatch' \
    exec "$script_dir/run_fw550_system_exit_probe.sh"
