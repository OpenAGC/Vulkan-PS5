#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: $0 lifecycle|reset|idle|full" >&2
    exit 2
fi

stage=$1
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(dirname -- "$script_dir")
build_dir=${VULKAN_PS5_PROSPERO_BUILD:-$repo_dir/build-prospero-m2}

case "$stage" in
    lifecycle)
        elf="$build_dir/vulkan_ps5_query_lifecycle_probe.elf"
        expected='^query_lifecycle: PASS green=[0-9]+$'
        failure='query_lifecycle: mismatch'
        ;;
    reset)
        elf="$build_dir/vulkan_ps5_query_reset_probe.elf"
        expected='^query_reset: PASS green=[0-9]+$'
        failure='query_reset: mismatch'
        ;;
    idle)
        elf="$build_dir/vulkan_ps5_query_idle_probe.elf"
        expected='^query_idle: PASS samples=0 available=1$'
        failure='query_idle: mismatch'
        ;;
    full)
        elf="$build_dir/vulkan_ps5_query_example.elf"
        expected='^query: PASS samples=[0-9]+ green=[0-9]+$'
        failure='query: mismatch'
        ;;
    *)
        echo "usage: $0 lifecycle|reset|idle|full" >&2
        exit 2
        ;;
esac

VULKAN_PS5_EXIT_ELF="$elf" \
VULKAN_PS5_EXIT_REMOTE_NAME="vulkan_ps5_query_${stage}" \
VULKAN_PS5_EXIT_FILE_STEM="query-${stage}" \
VULKAN_PS5_EXIT_DISPLAY_NAME="query ${stage} probe" \
VULKAN_PS5_EXIT_SUCCESS_REGEX="$expected" \
VULKAN_PS5_EXIT_FAILURE_PATTERN="$failure" \
VULKAN_PS5_REQUIRE_CLEANUP=1 \
    exec "$script_dir/run_fw550_system_exit_probe.sh"
