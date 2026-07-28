#!/bin/sh
set -eu

: "${PS5_HOST:?set PS5_HOST to the FW 5.50 console address}"

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(dirname -- "$script_dir")
build_dir=${VULKAN_PS5_PROSPERO_BUILD:-$repo_dir/build-prospero-m2}
log_dir=${VULKAN_PS5_FW550_LOG_DIR:-$script_dir/qualification-logs}
klog_port=${VULKAN_PS5_KLOG_PORT:-3232}
klog_settle_delay=${VULKAN_PS5_KLOG_SETTLE_DELAY:-2}
pyps4debug_dir=${PYPS4DEBUG_DIR:-/Users/bizkut/Downloads/PS5/homebrew/PyPS4debug}
elf="$build_dir/vulkan_ps5_indirect_draw_probe.elf"

if [ ! -f "$elf" ]; then
    echo "missing Prospero indirect-draw probe: $elf" >&2
    exit 2
fi
if ! command -v nc >/dev/null 2>&1 || ! command -v uv >/dev/null 2>&1 || \
   [ ! -d "$pyps4debug_dir" ]; then
    echo "nc and ps5debug-NG/PyPS4debug are required" >&2
    exit 2
fi
if ! curl -sS --connect-timeout 3 --max-time 5 \
    "http://${PS5_HOST}:8080/" >/dev/null; then
    echo "FW 5.50 websrv is unreachable at ${PS5_HOST}:8080" >&2
    exit 1
fi

kill_exact_pid() {
    uv run --project "$pyps4debug_dir" python \
        "$script_dir/ps5debug_kill_process.py" --pid "$1" "$PS5_HOST"
}

latest_eboot_pid() {
    sed -n 's/^<\([0-9][0-9]*\)> EXEC \/app0\/eboot\.bin .*/\1/p' "$1" | \
        tail -n 1
}

mkdir -p "$log_dir"
timestamp=$(date -u +%Y%m%dT%H%M%SZ)
log="$log_dir/${timestamp}-indirect-draw-run1.log"
klog="$log_dir/${timestamp}-indirect-draw-run1.klog"
target_klog="$log_dir/${timestamp}-indirect-draw-run1-target.klog"

echo "FW550 indirect-draw run 1/1"
if ! "$script_dir/deploy_websrv.sh" "$elf" \
    vulkan_ps5_indirect_draw >"$log" 2>&1; then
    sleep "$klog_settle_delay"
    nc -w 5 "$PS5_HOST" "$klog_port" >"$klog" 2>&1 || true
    failed_pid=$(latest_eboot_pid "$klog")
    if [ -n "$failed_pid" ]; then
        kill_exact_pid "$failed_pid" || true
    fi
    sed -n '1,160p' "$log" >&2
    echo "indirect-draw deployment failed; log: $log" >&2
    exit 1
fi

sleep "$klog_settle_delay"
if ! nc -w 5 "$PS5_HOST" "$klog_port" >"$klog" 2>&1 || [ ! -s "$klog" ]; then
    echo "indirect-draw klog capture failed: $klog" >&2
    exit 1
fi
sed -n '1,160p' "$log"
target_pid=$(latest_eboot_pid "$klog")
if [ -z "$target_pid" ]; then
    echo "kernel log did not identify the indirect-draw PID: $klog" >&2
    exit 1
fi
target_exec_line=$(grep -n "^<${target_pid}> EXEC /app0/eboot\.bin " "$klog" | \
    tail -n 1 | cut -d: -f1)
sed -n "${target_exec_line},\$p" "$klog" >"$target_klog"
target_pid_hex=$(printf '%x' "$target_pid")
if ! grep -E '^indirect_draw: PASS green=[0-9]+ blue=[0-9]+ firstVertex=1 firstInstance=1,2 draws=2$' \
        "$log" >/dev/null || \
   grep -Eq \
    "# proc ID: *${target_pid}$|mDBG: Sending signal\(pid: *${target_pid},|App Crash : PID=0x0*${target_pid_hex}([^0-9a-f]|$)|SYSTEM_XO_VIOLATION" \
        "$target_klog"; then
    kill_exact_pid "$target_pid" || true
    echo "indirect-draw oracle or PID-scoped klog failed: $target_klog" >&2
    exit 1
fi

if ! uv run --project "$pyps4debug_dir" python \
    "$script_dir/ps5debug_kill_process.py" --assert-absent \
    --pid "$target_pid" "$PS5_HOST"; then
    kill_exact_pid "$target_pid" || true
    echo "indirect-draw process remained after return: $target_klog" >&2
    exit 1
fi
if ! curl -sS --connect-timeout 3 --max-time 5 \
    "http://${PS5_HOST}:8080/" >/dev/null; then
    echo "indirect-draw completed but console probe failed: $log" >&2
    exit 1
fi

warning='[KERNEL] WARNING: VM resource leak: set:1, res:0, amount:0x4000'
warning_count=$(grep -Fxc "$warning" "$target_klog" || true)
if grep -F '[KERNEL] WARNING:' "$target_klog" | grep -Fvx "$warning" \
    >/dev/null || [ "$warning_count" -gt 1 ]; then
    echo "indirect-draw produced an unexpected kernel warning: $target_klog" >&2
    exit 1
fi

echo "FW550 indirect-draw: PASS (multi/first-instance readback and clean PID-scoped klog)"
echo "log: $log"
echo "klog: $target_klog"
