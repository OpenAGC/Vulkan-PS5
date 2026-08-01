#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(dirname -- "$script_dir")

export VULKAN_PS5_EXIT_ELF="${VULKAN_PS5_EXIT_ELF:-$repo_dir/build-prospero-m2/vulkan_ps5_bc_copy_probe.elf}"
export VULKAN_PS5_EXIT_REMOTE_NAME="${VULKAN_PS5_EXIT_REMOTE_NAME:-vulkan-ps5-bc-copy}"
export VULKAN_PS5_EXIT_FILE_STEM="${VULKAN_PS5_EXIT_FILE_STEM:-bc-copy}"
export VULKAN_PS5_EXIT_DISPLAY_NAME="${VULKAN_PS5_EXIT_DISPLAY_NAME:-BC copy probe}"
export VULKAN_PS5_EXIT_SUCCESS_REGEX="${VULKAN_PS5_EXIT_SUCCESS_REGEX:-^bc_copy: PASS formats=14 regions=28 exact-bytes mips=2$}"
export VULKAN_PS5_EXIT_FAILURE_PATTERN="${VULKAN_PS5_EXIT_FAILURE_PATTERN:-bc_copy: mismatch}"
export VULKAN_PS5_REQUIRE_CLEANUP="${VULKAN_PS5_REQUIRE_CLEANUP:-1}"
export VULKAN_PS5_EXIT_EXPECTED_SHA256="${VULKAN_PS5_EXIT_EXPECTED_SHA256:-0c97df8a72c21577c543dd64649d3f3fc5e0e7f74190adf4ab2ba235bd3b74d4}"
export VULKAN_PS5_CLEANUP_EXPECTED_SHA256="${VULKAN_PS5_CLEANUP_EXPECTED_SHA256:-9fd6b41cf2ea87989c4217234c6f34c96a1ca5dc482355af1258539db77d4d76}"
export VULKAN_PS5_EXIT_MAX_SECONDS="${VULKAN_PS5_EXIT_MAX_SECONDS:-20}"

exec "$script_dir/run_fw550_system_exit_probe.sh" "$@"
