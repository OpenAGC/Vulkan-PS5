#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
build_dir=${VULKAN_PS5_PROSPERO_BUILD:-"$script_dir/../build-prospero-m2"}

VULKAN_PS5_EXIT_ELF="$build_dir/vulkan_ps5_shader_image_gather_probe.elf" \
VULKAN_PS5_EXIT_REMOTE_NAME=vulkan_ps5_shader_image_gather \
VULKAN_PS5_EXIT_FILE_STEM=shader-image-gather-run1 \
VULKAN_PS5_EXIT_DISPLAY_NAME='shader-image-gather probe' \
VULKAN_PS5_EXIT_SUCCESS_REGEX='^shader_image_gather_extended: PASS covered=18432 center=ffffffff offsets=4$' \
VULKAN_PS5_EXIT_FAILURE_PATTERN='shader_image_gather_extended: mismatch' \
    exec "$script_dir/run_fw550_system_exit_probe.sh"
