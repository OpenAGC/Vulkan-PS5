#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(dirname -- "$script_dir")
build_dir=${VULKAN_PS5_PROSPERO_BUILD:-$repo_dir/build-prospero-m2}

VULKAN_PS5_EXIT_ELF="$build_dir/vulkan_ps5_bc_sampling_smoke_probe.elf" \
VULKAN_PS5_EXIT_REMOTE_NAME=vulkan_ps5_bc_sampling_smoke \
VULKAN_PS5_EXIT_FILE_STEM=bc-sampling-smoke-run1 \
VULKAN_PS5_EXIT_DISPLAY_NAME='BC1/3/5/6H/7 sampling smoke probe' \
VULKAN_PS5_EXIT_SUCCESS_REGEX='^bc_sampling: PASS formats=5 sampled=5 exact-blocks$' \
VULKAN_PS5_EXIT_FAILURE_PATTERN='bc_sampling: mismatch' \
VULKAN_PS5_REQUIRE_CLEANUP=1 \
VULKAN_PS5_EXIT_EXPECTED_SHA256=5ac66e2a08cf9da77b64b863d0ce15dd0fded16522f6410150d167ff767abe6d \
VULKAN_PS5_CLEANUP_EXPECTED_SHA256=9fd6b41cf2ea87989c4217234c6f34c96a1ca5dc482355af1258539db77d4d76 \
    exec "$script_dir/run_fw550_system_exit_probe.sh"
