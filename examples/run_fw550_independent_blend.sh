#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(dirname -- "$script_dir")
build_dir=${VULKAN_PS5_PROSPERO_BUILD:-$repo_dir/build-prospero-m2}

VULKAN_PS5_EXIT_ELF="$build_dir/vulkan_ps5_independent_blend_probe.elf" \
VULKAN_PS5_EXIT_REMOTE_NAME=vulkan_ps5_independent_blend \
VULKAN_PS5_EXIT_FILE_STEM=independent-blend-run1 \
VULKAN_PS5_EXIT_DISPLAY_NAME='independent-blend probe' \
VULKAN_PS5_EXIT_SUCCESS_REGEX='^independent_blend: PASS target0=[0-9]+ target1=[0-9]+ color1=(7f7f007f|80800080)$' \
VULKAN_PS5_EXIT_FAILURE_PATTERN='independent_blend: mismatch' \
    exec "$script_dir/run_fw550_system_exit_probe.sh"
