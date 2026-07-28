#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
build_dir=${VULKAN_PS5_PROSPERO_BUILD:-"$script_dir/../build-prospero-m2"}

VULKAN_PS5_EXIT_ELF="$build_dir/vulkan_ps5_robust_buffer_probe.elf" \
VULKAN_PS5_EXIT_REMOTE_NAME=vulkan_ps5_robust_buffer \
VULKAN_PS5_EXIT_FILE_STEM=robust-buffer-run1 \
VULKAN_PS5_EXIT_DISPLAY_NAME='robust-buffer-access probe' \
VULKAN_PS5_EXIT_SUCCESS_REGEX='^robust_buffer_access: PASS OOB read=0 OOB store=discarded$' \
VULKAN_PS5_EXIT_FAILURE_PATTERN='robust_buffer_access: mismatch' \
    exec "$script_dir/run_fw550_system_exit_probe.sh"
