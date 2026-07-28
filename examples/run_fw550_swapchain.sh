#!/bin/sh
set -eu

: "${PS5_HOST:?set PS5_HOST to the FW 5.50 console address}"

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(dirname -- "$script_dir")
build_dir=${VULKAN_PS5_PROSPERO_BUILD:-$repo_dir/build-prospero-m2}
log_dir=${VULKAN_PS5_FW550_LOG_DIR:-$script_dir/qualification-logs}
websrv_timeout=${VULKAN_PS5_WEBSRV_TIMEOUT:-60}
pyps4debug_dir=${PYPS4DEBUG_DIR:-/Users/bizkut/Downloads/PS5/homebrew/PyPS4debug}
elf="$build_dir/vulkan_ps5_swapchain_example.elf"
remote_name=vulkan_ps5_swapchain

if [ ! -f "$elf" ]; then
    echo "missing Prospero sample: $elf" >&2
    exit 2
fi
if ! curl -sS --connect-timeout 3 --max-time 5 \
    "http://${PS5_HOST}:8080/" >/dev/null; then
    echo "FW 5.50 websrv is unreachable at ${PS5_HOST}:8080" >&2
    exit 1
fi

kill_stale_process() {
    if command -v uv >/dev/null 2>&1 && [ -d "$pyps4debug_dir" ]; then
        uv run --project "$pyps4debug_dir" python \
            "$script_dir/ps5debug_kill_process.py" "$PS5_HOST" \
            vulkan_ps5_swa || true
    fi
}

mkdir -p "$log_dir"
timestamp=$(date -u +%Y%m%dT%H%M%SZ)
log="$log_dir/${timestamp}-swapchain-run1.log"
echo "FW550 swapchain run 1/1"
if ! VULKAN_PS5_WEBSRV_TIMEOUT="$websrv_timeout" \
    "$script_dir/deploy_websrv.sh" "$elf" "$remote_name" >"$log" 2>&1; then
    kill_stale_process
    sed -n '1,200p' "$log" >&2
    echo "swapchain run failed; log: $log" >&2
    exit 1
fi
sed -n '1,200p' "$log"
if ! grep -E '^swapchain: PASS 1800 frames$' "$log" >/dev/null; then
    kill_stale_process
    echo "swapchain run did not produce its PASS oracle; log: $log" >&2
    exit 1
fi
if ! curl -sS --connect-timeout 3 --max-time 5 \
    "http://${PS5_HOST}:8080/" >/dev/null; then
    echo "swapchain passed but the post-run console probe failed; log: $log" >&2
    exit 1
fi

echo "FW550 swapchain: PASS (one bounded 1800-frame run)"
echo "log: $log"
