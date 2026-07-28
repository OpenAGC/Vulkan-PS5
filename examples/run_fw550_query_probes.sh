#!/bin/sh
set -eu

: "${PS5_HOST:?set PS5_HOST to the FW 5.50 console address}"

if [ "$#" -ne 1 ]; then
    echo "usage: $0 lifecycle|reset|idle|full" >&2
    exit 2
fi

stage=$1
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(dirname -- "$script_dir")
build_dir=${VULKAN_PS5_PROSPERO_BUILD:-$repo_dir/build-prospero-m2}
log_dir=${VULKAN_PS5_FW550_LOG_DIR:-$script_dir/qualification-logs}

case "$stage" in
    lifecycle)
        elf="$build_dir/vulkan_ps5_query_lifecycle_probe.elf"
        expected='^query_lifecycle: PASS green=[0-9]+$'
        ;;
    reset)
        elf="$build_dir/vulkan_ps5_query_reset_probe.elf"
        expected='^query_reset: PASS green=[0-9]+$'
        ;;
    idle)
        elf="$build_dir/vulkan_ps5_query_idle_probe.elf"
        expected='^query_idle: PASS samples=0 available=1$'
        ;;
    full)
        elf="$build_dir/vulkan_ps5_query_example.elf"
        expected='^query: PASS samples=[0-9]+ green=[0-9]+$'
        ;;
    *)
        echo "usage: $0 lifecycle|reset|idle|full" >&2
        exit 2
        ;;
esac

if [ ! -f "$elf" ]; then
    echo "missing Prospero probe: $elf" >&2
    exit 2
fi
if ! curl -sS --connect-timeout 3 --max-time 5 \
    "http://${PS5_HOST}:8080/" >/dev/null; then
    echo "FW 5.50 websrv is unreachable at ${PS5_HOST}:8080" >&2
    exit 1
fi

mkdir -p "$log_dir"
timestamp=$(date -u +%Y%m%dT%H%M%SZ)
log="$log_dir/${timestamp}-query-${stage}.log"
echo "FW550 query probe: $stage"
if ! "$script_dir/deploy_websrv.sh" "$elf" \
    "vulkan_ps5_query_${stage}" >"$log" 2>&1; then
    sed -n '1,160p' "$log" >&2
    echo "query $stage probe failed; log: $log" >&2
    exit 1
fi
sed -n '1,160p' "$log"
if ! grep -E "$expected" "$log" >/dev/null; then
    echo "query $stage probe did not produce its PASS oracle; log: $log" >&2
    exit 1
fi
echo "FW550 query probe: PASS ($stage)"
echo "log: $log"
