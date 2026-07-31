#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
build_dir=${VULKAN_PS5_PROSPERO_BUILD:-"$script_dir/../build-prospero-m2"}

VULKAN_PS5_EXIT_ELF="$build_dir/vulkan_ps5_alpha_to_one_probe.elf" \
VULKAN_PS5_EXIT_REMOTE_NAME=vulkan_ps5_alpha_to_one \
VULKAN_PS5_EXIT_FILE_STEM=alpha-to-one-run1 \
VULKAN_PS5_EXIT_DISPLAY_NAME='alpha-to-one probe' \
VULKAN_PS5_REQUIRE_CLEANUP=1 \
VULKAN_PS5_EXIT_SUCCESS_REGEX='^alpha_to_one: PASS [0-9][0-9]* green pixels$' \
VULKAN_PS5_EXIT_FAILURE_PATTERN='alpha_to_one: mismatch' \
    exec "$script_dir/run_fw550_system_exit_probe.sh"
