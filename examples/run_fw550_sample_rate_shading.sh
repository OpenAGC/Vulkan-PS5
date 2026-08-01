#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(dirname -- "$script_dir")
build_dir=${VULKAN_PS5_PROSPERO_BUILD:-$repo_dir/build-prospero-m2}

VULKAN_PS5_EXIT_ELF="$build_dir/vulkan_ps5_sample_rate_shading_probe.elf" \
VULKAN_PS5_EXIT_REMOTE_NAME=vulkan_ps5_sample_rate_shading \
VULKAN_PS5_EXIT_FILE_STEM=sample-rate-shading-run1 \
VULKAN_PS5_EXIT_DISPLAY_NAME='sample-rate shading probe' \
VULKAN_PS5_EXIT_SUCCESS_REGEX='^sample_rate_shading: PASS samples=(18336,18528|18528,18336),18432,18432 total=73728$' \
VULKAN_PS5_EXIT_FAILURE_PATTERN='sample_rate_shading: mismatch' \
    exec "$script_dir/run_fw550_system_exit_probe.sh"
