#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
build_dir=${VULKAN_PS5_PROSPERO_BUILD:-"$script_dir/../build-prospero-m2"}

VULKAN_PS5_EXIT_ELF="$build_dir/vulkan_ps5_custom_border_color_probe.elf" \
VULKAN_PS5_CLEANUP_ELF="$build_dir/vulkan_ps5_process_cleanup.elf" \
VULKAN_PS5_EXIT_REMOTE_NAME=vulkan_ps5_custom_border_color \
VULKAN_PS5_EXIT_FILE_STEM=custom-border-color-run1 \
VULKAN_PS5_EXIT_DISPLAY_NAME='custom-border-color probe' \
VULKAN_PS5_REQUIRE_CLEANUP=1 \
VULKAN_PS5_EXIT_SUCCESS_REGEX='^custom_border_color: PASS covered=[0-9][0-9]* blue=[0-9][0-9]* swizzle=BR$' \
VULKAN_PS5_EXIT_FAILURE_PATTERN='custom_border_color: mismatch' \
    exec "$script_dir/run_fw550_system_exit_probe.sh"
