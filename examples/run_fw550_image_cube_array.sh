#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(dirname -- "$script_dir")
build_dir=${VULKAN_PS5_PROSPERO_BUILD:-$repo_dir/build-prospero-m2}

VULKAN_PS5_EXIT_ELF="$build_dir/vulkan_ps5_image_cube_array_probe.elf" \
VULKAN_PS5_EXIT_REMOTE_NAME=vulkan_ps5_image_cube_array \
VULKAN_PS5_EXIT_FILE_STEM=image-cube-array-run1 \
VULKAN_PS5_EXIT_DISPLAY_NAME='image cube-array probe' \
VULKAN_PS5_EXIT_SUCCESS_REGEX='^image_cube_array: PASS covered=18432 red=9216 green=9216 cubes=2$' \
VULKAN_PS5_EXIT_FAILURE_PATTERN='image_cube_array: mismatch' \
    exec "$script_dir/run_fw550_system_exit_probe.sh"
