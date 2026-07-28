#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
build_dir=${VULKAN_PS5_PROSPERO_BUILD:-"$script_dir/../build-prospero-m2"}

VULKAN_PS5_EXIT_ELF="$build_dir/vulkan_ps5_storage_image_probe.elf" \
VULKAN_PS5_EXIT_REMOTE_NAME=vulkan_ps5_storage_image \
VULKAN_PS5_EXIT_FILE_STEM=storage-image-run1 \
VULKAN_PS5_EXIT_DISPLAY_NAME='storage-image write-without-format probe' \
VULKAN_PS5_EXIT_SUCCESS_REGEX='^storage_image_write_without_format: PASS 4096 deterministic pixels$' \
VULKAN_PS5_EXIT_FAILURE_PATTERN='storage_image_write_without_format: mismatch' \
    exec "$script_dir/run_fw550_system_exit_probe.sh"
