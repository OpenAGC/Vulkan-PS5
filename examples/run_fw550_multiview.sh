#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
build_dir=${VULKAN_PS5_PROSPERO_BUILD:-"$script_dir/../build-prospero-msaa"}

VULKAN_PS5_EXIT_ELF="$build_dir/vulkan_ps5_multiview_probe.elf" \
VULKAN_PS5_CLEANUP_ELF="$build_dir/vulkan_ps5_process_cleanup.elf" \
VULKAN_PS5_EXIT_REMOTE_NAME=vulkan_ps5_multiview \
VULKAN_PS5_EXIT_FILE_STEM=multiview-run \
VULKAN_PS5_EXIT_DISPLAY_NAME='multiview probe' \
VULKAN_PS5_REQUIRE_CLEANUP=1 \
VULKAN_PS5_EXIT_SUCCESS_REGEX='^multiview: PASS mask=0x21 view0=ff0000ff view5=ffff0000$' \
VULKAN_PS5_EXIT_FAILURE_PATTERN='multiview: FAIL' \
    exec "$script_dir/run_fw550_system_exit_probe.sh"
