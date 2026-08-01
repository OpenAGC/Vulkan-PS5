#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(dirname -- "$script_dir")
build_dir=${VULKAN_PS5_PROSPERO_BUILD:-$repo_dir/build-prospero-m2}

VULKAN_PS5_EXIT_ELF="$build_dir/vulkan_ps5_format_sampling_probe.elf" \
VULKAN_PS5_EXIT_REMOTE_NAME=vulkan_ps5_format_sampling \
VULKAN_PS5_EXIT_FILE_STEM=format-sampling-run1 \
VULKAN_PS5_EXIT_DISPLAY_NAME='scalar/vector sampled-image probe' \
VULKAN_PS5_EXIT_SUCCESS_REGEX='^format_sampling: PASS formats=38 components=152 exact-bits$' \
VULKAN_PS5_EXIT_FAILURE_PATTERN='format_sampling: mismatch' \
VULKAN_PS5_REQUIRE_CLEANUP=1 \
    exec "$script_dir/run_fw550_system_exit_probe.sh"
