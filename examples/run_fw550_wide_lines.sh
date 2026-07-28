#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
build_dir=${VULKAN_PS5_PROSPERO_BUILD:-"$script_dir/../build-prospero"}

VULKAN_PS5_EXIT_ELF="$build_dir/vulkan_ps5_wide_lines_probe.elf" \
VULKAN_PS5_EXIT_REMOTE_NAME=vulkan_ps5_wide_lines \
VULKAN_PS5_EXIT_FILE_STEM=wide-lines-run1 \
VULKAN_PS5_EXIT_DISPLAY_NAME='wide-lines probe' \
VULKAN_PS5_EXIT_SUCCESS_REGEX='^wide_lines: PASS width8=1024 width16=2048 width32=4096 center=ff0000ff$' \
VULKAN_PS5_EXIT_FAILURE_PATTERN='wide_lines: mismatch' \
    exec "$script_dir/run_fw550_system_exit_probe.sh"
