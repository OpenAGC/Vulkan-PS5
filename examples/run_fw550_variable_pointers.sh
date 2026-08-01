#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
build_dir=${VULKAN_PS5_PROSPERO_BUILD:-"$script_dir/../build-prospero-m2"}

VULKAN_PS5_EXIT_ELF="$build_dir/vulkan_ps5_variable_pointers_probe.elf" \
VULKAN_PS5_EXIT_REMOTE_NAME=vulkan_ps5_variable_pointers \
VULKAN_PS5_EXIT_FILE_STEM=variable-pointers-run1 \
VULKAN_PS5_EXIT_DISPLAY_NAME='variable-pointers probe' \
VULKAN_PS5_EXIT_SUCCESS_REGEX='^variable_pointers: PASS invocations=64 storage_load=64 storage_store=64 workgroup=64$' \
VULKAN_PS5_EXIT_FAILURE_PATTERN='variable_pointers: mismatch' \
    exec "$script_dir/run_fw550_system_exit_probe.sh"
