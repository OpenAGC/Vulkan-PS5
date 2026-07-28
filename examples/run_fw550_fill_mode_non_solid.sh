#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(dirname -- "$script_dir")
build_dir=${VULKAN_PS5_PROSPERO_BUILD:-$repo_dir/build-prospero-m2}

VULKAN_PS5_EXIT_ELF="$build_dir/vulkan_ps5_fill_mode_non_solid_probe.elf" \
VULKAN_PS5_EXIT_REMOTE_NAME=vulkan_ps5_fill_mode_non_solid \
VULKAN_PS5_EXIT_FILE_STEM=fill-mode-non-solid-run1 \
VULKAN_PS5_EXIT_DISPLAY_NAME='fill-mode-non-solid probe' \
VULKAN_PS5_EXIT_SUCCESS_REGEX='^fill_mode_non_solid: PASS line=[0-9]+ point=[0-9]+ center=00000000$' \
VULKAN_PS5_EXIT_FAILURE_PATTERN='fill_mode_non_solid: mismatch' \
    exec "$script_dir/run_fw550_system_exit_probe.sh"
