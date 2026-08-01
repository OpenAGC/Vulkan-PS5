#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(dirname -- "$script_dir")
build_dir=${VULKAN_PS5_PROSPERO_BUILD:-$repo_dir/build-prospero-depthclip}

VULKAN_PS5_EXIT_ELF="$build_dir/vulkan_ps5_depth_clip_enable_probe.elf" \
VULKAN_PS5_CLEANUP_ELF="$build_dir/vulkan_ps5_process_cleanup.elf" \
VULKAN_PS5_EXIT_REMOTE_NAME=vulkan_ps5_depth_clip_enable \
VULKAN_PS5_EXIT_FILE_STEM=fw1160-depth-clip-enable \
VULKAN_PS5_EXIT_DISPLAY_NAME='depth-clip-enable probe' \
VULKAN_PS5_EXIT_SUCCESS_REGEX='^depth_clip_enable: PASS green=[0-9]+ red=[0-9]+ raw=[0-9]+/[0-9]+/[0-9]+ stencil=[0-9]+$' \
VULKAN_PS5_EXIT_FAILURE_PATTERN='depth_clip_enable: mismatch' \
VULKAN_PS5_REQUIRE_CLEANUP=1 \
VULKAN_PS5_ENDPOINT_LABEL=FW1160 \
VULKAN_PS5_LIVE_KLOG=1 \
VULKAN_PS5_ALLOW_NO_KLOG=1 \
    exec "$script_dir/run_fw550_system_exit_probe.sh"
