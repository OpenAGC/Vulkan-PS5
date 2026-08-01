#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(dirname -- "$script_dir")
build_dir=${VULKAN_PS5_PROSPERO_BUILD:-$repo_dir/build-prospero-m2}

VULKAN_PS5_EXIT_ELF="$build_dir/vulkan_ps5_format_storage_probe.elf" \
VULKAN_PS5_EXIT_REMOTE_NAME=vulkan_ps5_format_storage \
VULKAN_PS5_EXIT_FILE_STEM=format-storage-run1 \
VULKAN_PS5_EXIT_DISPLAY_NAME='scalar/vector storage-image probe' \
VULKAN_PS5_EXIT_SUCCESS_REGEX='^format_storage: PASS formats=30 pixels=480 exact-bits$' \
VULKAN_PS5_EXIT_FAILURE_PATTERN='format_storage: mismatch' \
VULKAN_PS5_REQUIRE_CLEANUP=1 \
    exec "$script_dir/run_fw550_system_exit_probe.sh"
