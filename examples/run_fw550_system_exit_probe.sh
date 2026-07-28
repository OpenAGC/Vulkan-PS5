#!/bin/sh
set -eu

: "${PS5_HOST:?set PS5_HOST to the FW 5.50 console address}"

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(dirname -- "$script_dir")
build_dir=${VULKAN_PS5_PROSPERO_BUILD:-$repo_dir/build-prospero-m2}
log_dir=${VULKAN_PS5_FW550_LOG_DIR:-$script_dir/qualification-logs}
websrv_timeout=${VULKAN_PS5_WEBSRV_TIMEOUT:-30}
klog_port=${VULKAN_PS5_KLOG_PORT:-3232}
klog_settle_delay=${VULKAN_PS5_KLOG_SETTLE_DELAY:-2}
pyps4debug_dir=${PYPS4DEBUG_DIR:-/Users/bizkut/Downloads/PS5/homebrew/PyPS4debug}
elf=${VULKAN_PS5_EXIT_ELF:-$build_dir/vulkan_ps5_system_exit_probe.elf}
remote_name=${VULKAN_PS5_EXIT_REMOTE_NAME:-vulkan_ps5_system_exit_probe}
file_stem=${VULKAN_PS5_EXIT_FILE_STEM:-system-exit-probe}
display_name=${VULKAN_PS5_EXIT_DISPLAY_NAME:-system-exit probe}
success_regex=${VULKAN_PS5_EXIT_SUCCESS_REGEX:-'^system-exit-probe: ready app=0x[0-9a-f]+$'}
failure_pattern=${VULKAN_PS5_EXIT_FAILURE_PATTERN:-'system-exit-probe: unexpected return'}

if [ ! -f "$elf" ]; then
    echo "missing Prospero probe: $elf" >&2
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
        "$script_dir/ps5debug_kill_process.py" --pid "$1" "$PS5_HOST" eboot.bin
}

latest_eboot_pid() {
    sed -n 's/^<\([0-9][0-9]*\)> EXEC \/app0\/eboot\.bin .*category=native_game.*/\1/p' "$1" | \
        tail -n 1
}

sanitize_klog() {
    sanitized="$1.sanitized"
    tr -d '\000' <"$1" >"$sanitized"
    mv "$sanitized" "$1"
}

mkdir -p "$log_dir"
timestamp=$(date -u +%Y%m%dT%H%M%SZ)
log="$log_dir/${timestamp}-${file_stem}.log"
klog="$log_dir/${timestamp}-${file_stem}.klog"
target_klog="$log_dir/${timestamp}-${file_stem}-target.klog"

echo "FW550 ${display_name} 1/1"
if ! VULKAN_PS5_WEBSRV_TIMEOUT="$websrv_timeout" \
    "$script_dir/deploy_websrv.sh" "$elf" "$remote_name" >"$log" 2>&1; then
    sleep "$klog_settle_delay"
    nc -w 5 "$PS5_HOST" "$klog_port" >"$klog" 2>&1 || true
    if [ -s "$klog" ]; then sanitize_klog "$klog"; fi
    failed_pid=$(latest_eboot_pid "$klog")
    if [ -n "$failed_pid" ]; then
        kill_exact_pid "$failed_pid" || true
    fi
    sed -n '1,160p' "$log" >&2
    echo "${display_name} deployment failed; log: $log" >&2
    exit 1
fi

sleep "$klog_settle_delay"
if ! nc -w 5 "$PS5_HOST" "$klog_port" >"$klog" 2>&1 || [ ! -s "$klog" ]; then
    echo "${display_name} klog capture failed: $klog" >&2
    exit 1
fi
sanitize_klog "$klog"
sed -n '1,160p' "$log"
if ! grep -E "$success_regex" "$log" >/dev/null || \
   grep -F "$failure_pattern" "$log" >/dev/null; then
    failed_pid=$(latest_eboot_pid "$klog")
    if [ -n "$failed_pid" ]; then
        kill_exact_pid "$failed_pid" || true
    fi
    echo "${display_name} did not reach its self-kill oracle: $log" >&2
    exit 1
fi

target_pid=$(latest_eboot_pid "$klog")
if [ -z "$target_pid" ]; then
    echo "kernel log did not identify the ${display_name} PID: $klog" >&2
    exit 1
fi
target_exec_line=$(grep -n "^<${target_pid}> EXEC /app0/eboot\.bin " "$klog" | \
    tail -n 1 | cut -d: -f1)
sed -n "${target_exec_line},\$p" "$klog" >"$target_klog"
target_pid_hex=$(printf '%x' "$target_pid")
if grep -Eq \
    "# proc ID: *${target_pid}$|mDBG: Sending signal\(pid: *${target_pid},|App Crash : PID=0x0*${target_pid_hex}([^0-9a-f]|$)|SYSTEM_XO_VIOLATION" \
    "$target_klog"; then
    kill_exact_pid "$target_pid" || true
    echo "${display_name} hit a fatal lifecycle fault: $target_klog" >&2
    exit 1
fi

kill_pair=$(sed -n \
    's/.*KillApp() appId={0x\([0-9A-Fa-f][0-9A-Fa-f]*\)} is requested from 0x\([0-9A-Fa-f][0-9A-Fa-f]*\).*/\1 \2/p' \
    "$target_klog" | tail -n 1)
kill_line=$(grep -n 'KillApp() appId=' "$target_klog" | tail -n 1 | \
    cut -d: -f1 || true)
all_exited_line=$(grep -n '\[AppMgr\] All processes exited' "$target_klog" | \
    tail -n 1 | cut -d: -f1 || true)
if [ -z "$kill_pair" ] || [ -z "$kill_line" ] || \
   [ -z "$all_exited_line" ]; then
    kill_exact_pid "$target_pid" || true
    echo "${display_name} lifecycle evidence is incomplete: $target_klog" >&2
    exit 1
fi
kill_app=${kill_pair%% *}
requester_app=${kill_pair#* }
if [ "$((0x$kill_app))" -ne "$((0x$requester_app))" ] || \
   [ "$all_exited_line" -le "$kill_line" ]; then
    kill_exact_pid "$target_pid" || true
    echo "${display_name} lifecycle evidence is inconsistent: $target_klog" >&2
    exit 1
fi

if ! uv run --project "$pyps4debug_dir" python \
    "$script_dir/ps5debug_kill_process.py" --assert-absent \
    --pid "$target_pid" "$PS5_HOST" eboot.bin; then
    kill_exact_pid "$target_pid" || true
    echo "${display_name} process remained after self-kill: $target_klog" >&2
    exit 1
fi
if ! curl -sS --connect-timeout 3 --max-time 5 \
    "http://${PS5_HOST}:8080/" >/dev/null; then
    echo "${display_name} completed but console probe failed: $log" >&2
    exit 1
fi

warning='[KERNEL] WARNING: VM resource leak: set:1, res:0, amount:0x4000'
warning_count=$(grep -Fxc "$warning" "$target_klog" || true)
if grep -F '[KERNEL] WARNING:' "$target_klog" | grep -Fvx "$warning" \
    >/dev/null || [ "$warning_count" -gt 1 ]; then
    echo "${display_name} produced an unexpected kernel warning: $target_klog" >&2
    exit 1
fi
if [ "$warning_count" -eq 1 ]; then
    echo "FW550 ${display_name}: BASELINE_VM_WARNING amount=0x4000"
else
    echo "FW550 ${display_name}: CLEAN"
fi
echo "log: $log"
echo "klog: $target_klog"
