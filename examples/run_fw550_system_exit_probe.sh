#!/bin/sh
set -eu

: "${PS5_HOST:?set PS5_HOST to the FW 5.50 console address}"

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(dirname -- "$script_dir")
build_dir=${VULKAN_PS5_PROSPERO_BUILD:-$repo_dir/build-prospero-m2}
log_dir=${VULKAN_PS5_FW550_LOG_DIR:-$script_dir/qualification-logs}
endpoint_label=${VULKAN_PS5_ENDPOINT_LABEL:-FW550}
websrv_timeout=${VULKAN_PS5_WEBSRV_TIMEOUT:-30}
klog_port=${VULKAN_PS5_KLOG_PORT:-3232}
klog_settle_delay=${VULKAN_PS5_KLOG_SETTLE_DELAY:-2}
live_klog=${VULKAN_PS5_LIVE_KLOG:-0}
allow_no_klog=${VULKAN_PS5_ALLOW_NO_KLOG:-0}
pyps4debug_dir=${PYPS4DEBUG_DIR:-/Users/bizkut/Downloads/PS5/homebrew/PyPS4debug}
elf=${VULKAN_PS5_EXIT_ELF:-$build_dir/vulkan_ps5_system_exit_probe.elf}
cleanup_elf=${VULKAN_PS5_CLEANUP_ELF:-$build_dir/vulkan_ps5_process_cleanup.elf}
require_cleanup=${VULKAN_PS5_REQUIRE_CLEANUP:-0}
remote_name=${VULKAN_PS5_EXIT_REMOTE_NAME:-vulkan_ps5_system_exit_probe}
file_stem=${VULKAN_PS5_EXIT_FILE_STEM:-system-exit-probe}
display_name=${VULKAN_PS5_EXIT_DISPLAY_NAME:-system-exit probe}
success_regex=${VULKAN_PS5_EXIT_SUCCESS_REGEX:-'^system-exit-probe: ready app=0x[0-9a-f]+$'}
failure_pattern=${VULKAN_PS5_EXIT_FAILURE_PATTERN:-'system-exit-probe: unexpected return'}

case "$live_klog" in
    0|1) ;;
    *) echo "VULKAN_PS5_LIVE_KLOG must be 0 or 1" >&2; exit 2 ;;
esac
case "$allow_no_klog" in
    0|1) ;;
    *) echo "VULKAN_PS5_ALLOW_NO_KLOG must be 0 or 1" >&2; exit 2 ;;
esac

if [ ! -f "$elf" ]; then
    echo "missing Prospero probe: $elf" >&2
    exit 2
fi
if [ "$require_cleanup" = 1 ] && [ ! -f "$cleanup_elf" ]; then
    echo "missing Prospero cleanup prerequisite: $cleanup_elf" >&2
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

if [ "$require_cleanup" = 1 ]; then
    cleanup_dir=/data/homebrew/vulkan_ps5_process_cleanup
    curl -sS --connect-timeout 3 --max-time 30 \
        "ftp://${PS5_HOST}:2121/" --quote "MKD $cleanup_dir" \
        >/dev/null 2>&1 || true
    curl -sS --connect-timeout 3 --max-time 30 -T "$cleanup_elf" \
        "ftp://${PS5_HOST}:2121${cleanup_dir}/eboot.elf" >/dev/null
    curl -sS --connect-timeout 3 --max-time 10 \
        "http://${PS5_HOST}:8080/hbldr?pipe=0&daemon=1&path=${cleanup_dir}/eboot.elf" \
        >/dev/null
    sleep 2
    if ! curl -sS --connect-timeout 3 --max-time 5 \
        "http://${PS5_HOST}:8080/" >/dev/null; then
        echo "FW 5.50 cleanup prerequisite left websrv unreachable" >&2
        exit 1
    fi
fi

kill_exact_pid() {
    uv run --project "$pyps4debug_dir" python \
        "$script_dir/ps5debug_kill_process.py" --pid "$1" "$PS5_HOST" eboot.bin
}

assert_process_absent() {
    uv run --project "$pyps4debug_dir" python \
        "$script_dir/ps5debug_kill_process.py" --assert-absent \
        "$PS5_HOST" eboot.bin
    uv run --project "$pyps4debug_dir" python \
        "$script_dir/ps5debug_kill_process.py" --assert-absent \
        "$PS5_HOST" eboot.elf
}

accept_no_klog_fallback() {
    [ "$allow_no_klog" = 1 ] &&
        assert_process_absent &&
        curl -sS --connect-timeout 3 --max-time 5 \
            "http://${PS5_HOST}:8080/" >/dev/null
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
live_klog_pid=

stop_live_klog() {
    if [ -n "$live_klog_pid" ]; then
        kill "$live_klog_pid" >/dev/null 2>&1 || true
        wait "$live_klog_pid" >/dev/null 2>&1 || true
        live_klog_pid=
    fi
}
trap 'stop_live_klog' EXIT

if [ "$live_klog" = 1 ]; then
    nc -w 30 "$PS5_HOST" "$klog_port" >"$klog" 2>&1 &
    live_klog_pid=$!
    sleep 1
fi

echo "${endpoint_label} ${display_name} 1/1"
if ! VULKAN_PS5_WEBSRV_TIMEOUT="$websrv_timeout" \
    "$script_dir/deploy_websrv.sh" "$elf" "$remote_name" >"$log" 2>&1; then
    sleep "$klog_settle_delay"
    if [ "$live_klog" = 1 ]; then
        stop_live_klog
    else
        nc -w 5 "$PS5_HOST" "$klog_port" >"$klog" 2>&1 || true
    fi
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
if [ "$live_klog" = 1 ]; then
    stop_live_klog
elif ! nc -w 5 "$PS5_HOST" "$klog_port" >"$klog" 2>&1; then
    :
fi
sed -n '1,160p' "$log"
if ! grep -E "$success_regex" "$log" >/dev/null || \
   grep -F "$failure_pattern" "$log" >/dev/null || \
   grep -E '^vulkan-ps5: OpenAGC .* failed:' "$log" >/dev/null; then
    failed_pid=$(latest_eboot_pid "$klog")
    if [ -n "$failed_pid" ]; then
        kill_exact_pid "$failed_pid" || true
    fi
    echo "${display_name} did not reach its self-kill oracle: $log" >&2
    exit 1
fi
if [ ! -s "$klog" ]; then
    if ! accept_no_klog_fallback; then
        echo "${display_name} klog capture failed: $klog" >&2
        exit 1
    fi
    echo "${endpoint_label} ${display_name}: NO_KLOG_PROCESS_ABSENCE"
    echo "log: $log"
    echo "klog unavailable: $klog"
    exit 0
fi
sanitize_klog "$klog"

target_pid=$(latest_eboot_pid "$klog")
if [ -z "$target_pid" ]; then
    if accept_no_klog_fallback; then
        echo "${endpoint_label} ${display_name}: NO_KLOG_PROCESS_ABSENCE"
        echo "log: $log"
        echo "klog unavailable or unattributable: $klog"
        exit 0
    fi
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

self_kill=$(grep -n 'KillApp() appId=' "$target_klog" | sed -n \
    's/^\([0-9][0-9]*\):.*KillApp() appId={0x\([0-9A-Fa-f][0-9A-Fa-f]*\)} is requested from 0x\2.*/\1 \2/p' | \
    tail -n 1)
all_exited_line=$(grep -n '\[AppMgr\] All processes exited' "$target_klog" | \
    tail -n 1 | cut -d: -f1 || true)
if [ -z "$self_kill" ] || \
   [ -z "$all_exited_line" ]; then
    kill_exact_pid "$target_pid" || true
    echo "${display_name} lifecycle evidence is incomplete: $target_klog" >&2
    exit 1
fi
kill_line=${self_kill%% *}
if [ "$all_exited_line" -le "$kill_line" ]; then
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
    echo "${endpoint_label} ${display_name}: BASELINE_VM_WARNING amount=0x4000"
else
    echo "${endpoint_label} ${display_name}: CLEAN"
fi
echo "log: $log"
echo "klog: $target_klog"
