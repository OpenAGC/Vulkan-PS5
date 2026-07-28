#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(dirname -- "$script_dir")
build_dir=${VULKAN_PS5_PROSPERO_BUILD:-$repo_dir/build-prospero-m2}

VULKAN_PS5_EXIT_ELF="$build_dir/vulkan_ps5_multi_viewport_probe.elf" \
VULKAN_PS5_EXIT_REMOTE_NAME=vulkan_ps5_multi_viewport \
VULKAN_PS5_EXIT_FILE_STEM=multi-viewport-run1 \
VULKAN_PS5_EXIT_DISPLAY_NAME='multi-viewport probe' \
VULKAN_PS5_EXIT_SUCCESS_REGEX='^multi_viewport: PASS green=9216 red=9216 viewports=2$' \
VULKAN_PS5_EXIT_FAILURE_PATTERN='multi_viewport: mismatch' \
    exec "$script_dir/run_fw550_system_exit_probe.sh"
