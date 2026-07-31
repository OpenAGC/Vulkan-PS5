#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(dirname -- "$script_dir")
build_dir=${VULKAN_PS5_PROSPERO_BUILD:-$repo_dir/build-prospero-m2}

VULKAN_PS5_EXIT_ELF="$build_dir/vulkan_ps5_buffer_copy_example.elf" \
VULKAN_PS5_EXIT_REMOTE_NAME=vulkan_ps5_buffer_copy \
VULKAN_PS5_EXIT_FILE_STEM=buffer-copy-run1 \
VULKAN_PS5_EXIT_DISPLAY_NAME='buffer-copy probe' \
VULKAN_PS5_EXIT_SUCCESS_REGEX='^buffer_copy: PASS bytes=144 regions=2 guards=112$' \
VULKAN_PS5_EXIT_FAILURE_PATTERN='buffer_copy: mismatch' \
VULKAN_PS5_REQUIRE_CLEANUP=1 \
    exec "$script_dir/run_fw550_system_exit_probe.sh"
