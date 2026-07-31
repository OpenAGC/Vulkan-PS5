#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
build_dir=${VULKAN_PS5_PROSPERO_BUILD:-"$script_dir/../build-prospero-m2"}

VULKAN_PS5_EXIT_ELF="$build_dir/vulkan_ps5_scalar_block_layout_probe.elf" \
VULKAN_PS5_EXIT_REMOTE_NAME=vulkan_ps5_scalar_block_layout \
VULKAN_PS5_EXIT_FILE_STEM=scalar-block-layout-run1 \
VULKAN_PS5_EXIT_DISPLAY_NAME='scalar-block-layout probe' \
VULKAN_PS5_REQUIRE_CLEANUP=1 \
VULKAN_PS5_EXIT_SUCCESS_REGEX='^scalar_block_layout: PASS stride=12 result_offset=24 result=651a5a5a$' \
VULKAN_PS5_EXIT_FAILURE_PATTERN='scalar_block_layout: mismatch' \
    exec "$script_dir/run_fw550_system_exit_probe.sh"
