#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
build_dir=${VULKAN_PS5_PROSPERO_BUILD:-"$script_dir/../build-prospero"}

VULKAN_PS5_EXIT_ELF="$build_dir/vulkan_ps5_fragment_stores_atomics_probe.elf" \
VULKAN_PS5_EXIT_REMOTE_NAME=vulkan_ps5_fragment_stores_atomics \
VULKAN_PS5_EXIT_FILE_STEM=fragment-stores-atomics-run1 \
VULKAN_PS5_EXIT_DISPLAY_NAME='fragment-stores-atomics probe' \
VULKAN_PS5_EXIT_SUCCESS_REGEX='^fragment_stores_atomics: PASS covered=18432 atomic=18432 stores=18432 marker=51a7c0de$' \
VULKAN_PS5_EXIT_FAILURE_PATTERN='fragment_stores_atomics: mismatch' \
    exec "$script_dir/run_fw550_system_exit_probe.sh"
