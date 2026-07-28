#!/bin/sh
set -eu

: "${PS5_HOST:?set PS5_HOST to the FW 5.50 console address}"

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(dirname -- "$script_dir")
build_dir=${VULKAN_PS5_PROSPERO_BUILD:-$repo_dir/build-prospero-m2}
log_dir=${VULKAN_PS5_FW550_LOG_DIR:-$script_dir/qualification-logs}
websrv_timeout=${VULKAN_PS5_WEBSRV_TIMEOUT:-60}
klog_port=${VULKAN_PS5_KLOG_PORT:-3232}
klog_startup_delay=${VULKAN_PS5_KLOG_STARTUP_DELAY:-1}
klog_settle_delay=${VULKAN_PS5_KLOG_SETTLE_DELAY:-2}
pyps4debug_dir=${PYPS4DEBUG_DIR:-/Users/bizkut/Downloads/PS5/homebrew/PyPS4debug}
elf="$build_dir/vulkan_ps5_swapchain_example.elf"
remote_name=vulkan_ps5_swapchain

if [ ! -f "$elf" ]; then
    echo "missing Prospero sample: $elf" >&2
    exit 2
fi
case "$klog_port" in
    ''|*[!0-9]*|0)
        echo "VULKAN_PS5_KLOG_PORT must be a positive integer" >&2
        exit 2
        ;;
esac
case "$klog_startup_delay:$klog_settle_delay" in
    *[!0-9:]*|:*|*:)
        echo "kernel-log delays must be non-negative integers" >&2
        exit 2
        ;;
esac
if ! command -v nc >/dev/null 2>&1; then
    echo "nc is required for the bounded kernel-log gate" >&2
    exit 2
fi
if ! command -v uv >/dev/null 2>&1 || [ ! -d "$pyps4debug_dir" ]; then
    echo "ps5debug-NG/PyPS4debug is required for the process-exit gate" >&2
    exit 2
fi
if ! curl -sS --connect-timeout 3 --max-time 5 \
    "http://${PS5_HOST}:8080/" >/dev/null; then
    echo "FW 5.50 websrv is unreachable at ${PS5_HOST}:8080" >&2
    exit 1
fi

kill_stale_process() {
    uv run --project "$pyps4debug_dir" python \
        "$script_dir/ps5debug_kill_process.py" "$PS5_HOST" \
        vulkan_ps5_swa || true
}

mkdir -p "$log_dir"
timestamp=$(date -u +%Y%m%dT%H%M%SZ)
log="$log_dir/${timestamp}-swapchain-run1.log"
klog="$log_dir/${timestamp}-swapchain-run1.klog"
target_klog="$log_dir/${timestamp}-swapchain-run1-target.klog"
klog_pid=
stop_klog() {
    if [ -n "$klog_pid" ]; then
        kill "$klog_pid" 2>/dev/null || true
        wait "$klog_pid" 2>/dev/null || true
        klog_pid=
    fi
}
trap 'stop_klog' EXIT

nc "$PS5_HOST" "$klog_port" >"$klog" 2>&1 &
klog_pid=$!
sleep "$klog_startup_delay"
if ! kill -0 "$klog_pid" 2>/dev/null; then
    stop_klog
    echo "kernel-log stream is unreachable at ${PS5_HOST}:${klog_port}" >&2
    exit 1
fi

echo "FW550 swapchain run 1/1"
if ! VULKAN_PS5_WEBSRV_TIMEOUT="$websrv_timeout" \
    "$script_dir/deploy_websrv.sh" "$elf" "$remote_name" >"$log" 2>&1; then
    kill_stale_process
    sleep "$klog_settle_delay"
    stop_klog
    sed -n '1,200p' "$log" >&2
    echo "swapchain run failed; log: $log" >&2
    exit 1
fi
sleep "$klog_settle_delay"
stop_klog
sed -n '1,200p' "$log"
if ! grep -E '^swapchain: PASS 1800 frames$' "$log" >/dev/null; then
    kill_stale_process
    echo "swapchain run did not produce its PASS oracle; log: $log" >&2
    exit 1
fi

target_pid=$(sed -n \
    's/^<\([0-9][0-9]*\)> EXEC \/app0\/eboot\.bin .*/\1/p' "$klog" | tail -n 1)
if [ -z "$target_pid" ]; then
    echo "kernel log did not identify the launched eboot PID; klog: $klog" >&2
    exit 1
fi
target_exec_line=$(grep -n "^<${target_pid}> EXEC /app0/eboot\.bin " "$klog" | \
    tail -n 1 | cut -d: -f1)
sed -n "${target_exec_line},\$p" "$klog" >"$target_klog"
target_pid_hex=$(printf '%x' "$target_pid")
if grep -Eq \
    "# proc ID: *${target_pid}$|mDBG: Sending signal\(pid: *${target_pid},|App Crash : PID=0x0*${target_pid_hex}([^0-9a-f]|$)|SYSTEM_XO_VIOLATION|VM resource leak" \
    "$target_klog"; then
    sed -n '1,240p' "$target_klog" >&2
    echo "swapchain emitted PASS but its scoped kernel log is not clean: $target_klog" >&2
    exit 1
fi
if ! uv run --project "$pyps4debug_dir" python \
    "$script_dir/ps5debug_kill_process.py" --assert-absent \
    "$PS5_HOST" vulkan_ps5_swa; then
    echo "swapchain process remained after PASS; klog: $target_klog" >&2
    exit 1
fi
if ! curl -sS --connect-timeout 3 --max-time 5 \
    "http://${PS5_HOST}:8080/" >/dev/null; then
    echo "swapchain passed but the post-run console probe failed; log: $log" >&2
    exit 1
fi

echo "FW550 swapchain: PASS (1800 frames, clean exit and klog)"
echo "log: $log"
echo "klog: $target_klog"
