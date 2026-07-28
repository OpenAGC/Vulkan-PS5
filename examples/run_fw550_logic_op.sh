#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(dirname -- "$script_dir")
build_dir=${VULKAN_PS5_PROSPERO_BUILD:-$repo_dir/build-prospero-m2}

VULKAN_PS5_EXIT_ELF="$build_dir/vulkan_ps5_logic_op_probe.elf" \
VULKAN_PS5_EXIT_REMOTE_NAME=vulkan_ps5_logic_op \
VULKAN_PS5_EXIT_FILE_STEM=logic-op-run1 \
VULKAN_PS5_EXIT_DISPLAY_NAME='logic-op probe' \
VULKAN_PS5_EXIT_SUCCESS_REGEX='^logic_op: PASS xor=[0-9]+ background=[0-9]+ center=aaaacccc$' \
VULKAN_PS5_EXIT_FAILURE_PATTERN='logic_op: mismatch' \
    exec "$script_dir/run_fw550_system_exit_probe.sh"
