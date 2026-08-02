#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
build_dir=${VULKAN_PS5_PROSPERO_BUILD:-"$script_dir/../build-prospero-m2"}

VULKAN_PS5_EXIT_ELF="$build_dir/vulkan_ps5_subgroup_broadcast_probe.elf" \
VULKAN_PS5_EXIT_REMOTE_NAME=vulkan_ps5_subgroup_broadcast \
VULKAN_PS5_EXIT_FILE_STEM=subgroup-broadcast-run1 \
VULKAN_PS5_EXIT_DISPLAY_NAME='subgroup-broadcast probe' \
VULKAN_PS5_REQUIRE_CLEANUP=1 \
VULKAN_PS5_EXIT_SUCCESS_REGEX='^subgroup_broadcast_dynamic_id: PASS lanes=32 source=7 result=5a5a5a5d$' \
VULKAN_PS5_EXIT_FAILURE_PATTERN='subgroup_broadcast_dynamic_id: mismatch' \
    exec "$script_dir/run_fw550_system_exit_probe.sh"
