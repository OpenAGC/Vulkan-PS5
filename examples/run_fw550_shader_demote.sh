#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
build_dir=${VULKAN_PS5_PROSPERO_BUILD:-"$script_dir/../build-prospero"}

VULKAN_PS5_EXIT_ELF="$build_dir/vulkan_ps5_shader_demote_probe.elf" \
VULKAN_PS5_EXIT_REMOTE_NAME=vulkan_ps5_shader_demote \
VULKAN_PS5_EXIT_FILE_STEM=shader-demote-run1 \
VULKAN_PS5_EXIT_DISPLAY_NAME='shader-demote probe' \
VULKAN_PS5_EXIT_SUCCESS_REGEX='^shader_demote: PASS green=2048 blue=30720 demoted=32768 center=00000000$' \
VULKAN_PS5_EXIT_FAILURE_PATTERN='shader_demote: mismatch' \
    exec "$script_dir/run_fw550_system_exit_probe.sh"
