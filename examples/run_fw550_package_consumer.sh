#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(dirname -- "$script_dir")
build_dir=${VULKAN_PS5_PROSPERO_BUILD:-$repo_dir/build-prospero-m2}

VULKAN_PS5_EXIT_ELF="$build_dir/vulkan_ps5_package_consumer.elf"
VULKAN_PS5_EXIT_REMOTE_NAME=vulkan_ps5_package_consumer
VULKAN_PS5_EXIT_FILE_STEM=package-consumer
VULKAN_PS5_EXIT_DISPLAY_NAME='package consumer'
VULKAN_PS5_EXIT_SUCCESS_REGEX='^package-consumer: PASS result=0$'
VULKAN_PS5_EXIT_FAILURE_PATTERN='package-consumer: FAIL'
export VULKAN_PS5_EXIT_ELF VULKAN_PS5_EXIT_REMOTE_NAME
export VULKAN_PS5_EXIT_FILE_STEM VULKAN_PS5_EXIT_DISPLAY_NAME
export VULKAN_PS5_EXIT_SUCCESS_REGEX VULKAN_PS5_EXIT_FAILURE_PATTERN

exec "$script_dir/run_fw550_system_exit_probe.sh"
