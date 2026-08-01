#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
build_dir=${VULKAN_PS5_PROSPERO_BUILD:-"$script_dir/../build-prospero-m2"}

VULKAN_PS5_EXIT_ELF="$build_dir/vulkan_ps5_resolve_probe.elf" \
VULKAN_PS5_EXIT_REMOTE_NAME=vulkan_ps5_resolve \
VULKAN_PS5_EXIT_FILE_STEM=resolve-run1 \
VULKAN_PS5_EXIT_DISPLAY_NAME='resolve probe' \
VULKAN_PS5_REQUIRE_CLEANUP=1 \
VULKAN_PS5_EXIT_SUCCESS_REGEX='^resolve: PASS pixels=1024 color=ff00ff80 samples=4$' \
VULKAN_PS5_EXIT_FAILURE_PATTERN='resolve: mismatch' \
    exec "$script_dir/run_fw550_system_exit_probe.sh"
