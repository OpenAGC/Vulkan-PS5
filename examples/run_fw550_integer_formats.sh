#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(dirname -- "$script_dir")
build_dir=${VULKAN_PS5_PROSPERO_BUILD:-$repo_dir/build-prospero-m2}

VULKAN_PS5_EXIT_ELF="$build_dir/vulkan_ps5_integer_formats_probe.elf" \
VULKAN_PS5_EXIT_REMOTE_NAME=vulkan_ps5_integer_formats \
VULKAN_PS5_EXIT_FILE_STEM=integer-formats-run1 \
VULKAN_PS5_EXIT_DISPLAY_NAME='RGBA integer-format clear/readback probe' \
VULKAN_PS5_EXIT_SUCCESS_REGEX='^integer_formats: PASS formats=4 pixels=256 exact-bits$' \
VULKAN_PS5_EXIT_FAILURE_PATTERN='integer_formats: mismatch' \
VULKAN_PS5_REQUIRE_CLEANUP=1 \
    exec "$script_dir/run_fw550_system_exit_probe.sh"
