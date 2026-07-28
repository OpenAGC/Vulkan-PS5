#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
build_dir=${VULKAN_PS5_PROSPERO_BUILD:-"$script_dir/../build-prospero"}

VULKAN_PS5_EXIT_ELF="$build_dir/vulkan_ps5_shader_cull_distance_probe.elf" \
VULKAN_PS5_EXIT_REMOTE_NAME=vulkan_ps5_shader_cull_distance \
VULKAN_PS5_EXIT_FILE_STEM=shader-cull-distance-run1 \
VULKAN_PS5_EXIT_DISPLAY_NAME='shader-cull-distance probe' \
VULKAN_PS5_EXIT_SUCCESS_REGEX='^shader_cull_distance: PASS green=4608 left=00000000 right=ff00ff00$' \
VULKAN_PS5_EXIT_FAILURE_PATTERN='shader_cull_distance: mismatch' \
    exec "$script_dir/run_fw550_system_exit_probe.sh"
