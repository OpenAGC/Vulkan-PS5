#!/bin/sh
set -eu

: "${PS5_HOST:?set PS5_HOST to the FW 5.50 console address}"

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(dirname -- "$script_dir")
build_dir=${VULKAN_PS5_PROSPERO_BUILD:-$repo_dir/build-prospero-m2}
log_dir=${VULKAN_PS5_FW550_LOG_DIR:-$script_dir/qualification-logs}
websrv_timeout=${VULKAN_PS5_WEBSRV_TIMEOUT:-60}
klog_port=${VULKAN_PS5_KLOG_PORT:-3232}
klog_settle_delay=${VULKAN_PS5_KLOG_SETTLE_DELAY:-2}
pyps4debug_dir=${PYPS4DEBUG_DIR:-/Users/bizkut/Downloads/PS5/homebrew/PyPS4debug}
elf=${VULKAN_PS5_QUALIFICATION_ELF:-$build_dir/vulkan_ps5_swapchain_example.elf}
cleanup_elf=${VULKAN_PS5_CLEANUP_ELF:-$build_dir/vulkan_ps5_process_cleanup.elf}
remote_name=${VULKAN_PS5_QUALIFICATION_REMOTE_NAME:-vulkan_ps5_swapchain}
qualification_label=${VULKAN_PS5_QUALIFICATION_LABEL:-swapchain}
pass_pattern=${VULKAN_PS5_QUALIFICATION_PASS_PATTERN:-'^swapchain: PASS 1800 frames$'}
pass_description=${VULKAN_PS5_QUALIFICATION_PASS_DESCRIPTION:-'1800 frames'}
expected_sha256=${VULKAN_PS5_SWAPCHAIN_EXPECTED_SHA256:-}
expected_cleanup_sha256=${VULKAN_PS5_CLEANUP_EXPECTED_SHA256:-}
sidecar=${VULKAN_PS5_QUALIFICATION_SIDECAR:-}
sidecar_remote_name=${VULKAN_PS5_QUALIFICATION_SIDECAR_REMOTE_NAME:-}
expected_sidecar_sha256=${VULKAN_PS5_QUALIFICATION_SIDECAR_EXPECTED_SHA256:-}

if [ ! -f "$elf" ]; then
    echo "missing Prospero sample: $elf" >&2
    exit 2
fi
if [ ! -f "$cleanup_elf" ]; then
    echo "missing Prospero cleanup prerequisite: $cleanup_elf" >&2
    exit 2
fi
if [ -n "$sidecar" ]; then
    if [ ! -f "$sidecar" ]; then
        echo "missing qualification sidecar: $sidecar" >&2
        exit 2
    fi
    case "$sidecar_remote_name" in
        ''|*[!A-Za-z0-9_.-]*)
            echo "qualification sidecar remote name must use A-Z, a-z, 0-9, _, ., or -" >&2
            exit 2
            ;;
    esac
elif [ -n "$sidecar_remote_name" ] || [ -n "$expected_sidecar_sha256" ]; then
    echo "qualification sidecar path is required with sidecar metadata" >&2
    exit 2
fi
case "$remote_name" in
    ''|*[!A-Za-z0-9_-]*)
        echo "qualification remote name must use A-Z, a-z, 0-9, _, or -" >&2
        exit 2
        ;;
esac
case "$qualification_label" in
    ''|*[!A-Za-z0-9_-]*)
        echo "qualification label must use A-Z, a-z, 0-9, _, or -" >&2
        exit 2
        ;;
esac
if [ -z "$pass_pattern" ] || [ "${#pass_pattern}" -gt 256 ] ||
   [ -z "$pass_description" ] || [ "${#pass_description}" -gt 128 ]; then
    echo "qualification PASS pattern or description is empty or oversized" >&2
    exit 2
fi
case "$klog_port" in
    ''|*[!0-9]*|0)
        echo "VULKAN_PS5_KLOG_PORT must be a positive integer" >&2
        exit 2
        ;;
esac
case "$klog_settle_delay" in
    ''|*[!0-9]*)
        echo "VULKAN_PS5_KLOG_SETTLE_DELAY must be a non-negative integer" >&2
        exit 2
        ;;
esac
if ! command -v nc >/dev/null 2>&1 || \
   ! command -v shasum >/dev/null 2>&1; then
    echo "nc and shasum are required for the bounded qualification gate" >&2
    exit 2
fi
if ! command -v uv >/dev/null 2>&1 || [ ! -d "$pyps4debug_dir" ]; then
    echo "ps5debug-NG/PyPS4debug is required for the process-exit gate" >&2
    exit 2
fi

verify_local_sha256() {
    file=$1
    expected=$2
    label=$3
    if [ -z "$expected" ]; then
        echo "$label expected SHA-256 is required" >&2
        return 1
    fi
    actual=$(shasum -a 256 "$file" | awk '{print $1}')
    if [ "$actual" != "$expected" ]; then
        echo "$label SHA-256 mismatch: expected=$expected actual=$actual" >&2
        return 1
    fi
}

verify_remote_sha256() {
    remote_url=$1
    expected=$2
    label=$3
    downloaded=$(mktemp "${TMPDIR:-/tmp}/vulkan-ps5-upload.XXXXXX")
    if ! curl -sS --connect-timeout 3 --max-time 30 \
        -o "$downloaded" "$remote_url"; then
        rm -f "$downloaded"
        echo "$label remote SHA-256 download failed" >&2
        return 1
    fi
    actual=$(shasum -a 256 "$downloaded" | awk '{print $1}')
    rm -f "$downloaded"
    if [ "$actual" != "$expected" ]; then
        echo "$label remote SHA-256 mismatch: expected=$expected actual=$actual" >&2
        return 1
    fi
}

verify_local_sha256 "$elf" "$expected_sha256" 'swapchain ELF'
verify_local_sha256 "$cleanup_elf" "$expected_cleanup_sha256" \
    'cleanup prerequisite'
if [ -n "$sidecar" ]; then
    verify_local_sha256 "$sidecar" "$expected_sidecar_sha256" \
        'qualification sidecar'
fi
if ! curl -sS --connect-timeout 3 --max-time 5 \
    "http://${PS5_HOST}:8080/" >/dev/null; then
    echo "FW 5.50 websrv is unreachable at ${PS5_HOST}:8080" >&2
    exit 1
fi

cleanup_dir=/data/homebrew/vulkan_ps5_process_cleanup
curl -sS --connect-timeout 3 --max-time 30 \
    "ftp://${PS5_HOST}:2121/" --quote "MKD $cleanup_dir" \
    >/dev/null 2>&1 || true
curl -sS --connect-timeout 3 --max-time 30 -T "$cleanup_elf" \
    "ftp://${PS5_HOST}:2121${cleanup_dir}/eboot.elf" >/dev/null
verify_remote_sha256 \
    "ftp://${PS5_HOST}:2121${cleanup_dir}/eboot.elf" \
    "$expected_cleanup_sha256" 'cleanup prerequisite'
curl -sS --connect-timeout 3 --max-time 10 \
    "http://${PS5_HOST}:8080/hbldr?pipe=0&daemon=1&path=${cleanup_dir}/eboot.elf" \
    >/dev/null
sleep 2
if ! curl -sS --connect-timeout 3 --max-time 5 \
    "http://${PS5_HOST}:8080/" >/dev/null; then
    echo "FW 5.50 cleanup prerequisite left websrv unreachable" >&2
    exit 1
fi

kill_stale_process() {
    target_pid=$1
    uv run --project "$pyps4debug_dir" python \
        "$script_dir/ps5debug_kill_process.py" --pid "$target_pid" \
        "$PS5_HOST" eboot.bin
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
log="$log_dir/${timestamp}-swapchain-run1.log"
klog="$log_dir/${timestamp}-swapchain-run1.klog"
target_klog="$log_dir/${timestamp}-swapchain-run1-target.klog"
capture_klog() {
    sleep "$klog_settle_delay"
    nc -w 5 "$PS5_HOST" "$klog_port" >"$klog" 2>&1
    if [ ! -s "$klog" ]; then
        echo "kernel-log snapshot is empty: $klog" >&2
        return 1
    fi
    sanitize_klog "$klog"
}

echo "FW550 swapchain run 1/1"
remote_dir="/data/homebrew/$remote_name"
if ! {
    curl -sS --connect-timeout 3 --max-time 30 \
        "ftp://${PS5_HOST}:2121/" --quote "MKD $remote_dir" \
        >/dev/null 2>&1 || true
    curl -sS --connect-timeout 3 --max-time 30 -T "$elf" \
        "ftp://${PS5_HOST}:2121${remote_dir}/eboot.elf" >/dev/null
    verify_remote_sha256 \
        "ftp://${PS5_HOST}:2121${remote_dir}/eboot.elf" \
        "$expected_sha256" 'swapchain ELF'
    if [ -n "$sidecar" ]; then
        curl -sS --connect-timeout 3 --max-time 30 -T "$sidecar" \
            "ftp://${PS5_HOST}:2121${remote_dir}/${sidecar_remote_name}" >/dev/null
        verify_remote_sha256 \
            "ftp://${PS5_HOST}:2121${remote_dir}/${sidecar_remote_name}" \
            "$expected_sidecar_sha256" 'qualification sidecar'
    fi
    curl -sS --connect-timeout 3 --max-time "$websrv_timeout" \
        "http://${PS5_HOST}:8080/hbldr?pipe=1&daemon=0&path=${remote_dir}/eboot.elf"
} >"$log" 2>&1; then
    capture_klog || true
    if [ -s "$klog" ]; then
        failed_pid=$(latest_eboot_pid "$klog")
        if [ -n "$failed_pid" ]; then
            kill_stale_process "$failed_pid" || true
        fi
    fi
    sed -n '1,200p' "$log" >&2
    if [ -s "$klog" ]; then
        sed -n '1,240p' "$klog" >&2
    fi
    echo "swapchain run failed; log: $log" >&2
    exit 1
fi
if ! capture_klog; then
    echo "swapchain output completed but klog could not be verified" >&2
    exit 1
fi
sed -n '1,200p' "$log"
if ! grep -E "$pass_pattern" "$log" >/dev/null; then
    failed_pid=$(latest_eboot_pid "$klog")
    if [ -n "$failed_pid" ]; then
        kill_stale_process "$failed_pid" || true
    fi
    echo "swapchain run did not produce its PASS oracle; log: $log" >&2
    exit 1
fi

target_pid=$(latest_eboot_pid "$klog")
if [ -z "$target_pid" ]; then
    echo "kernel log did not identify the launched eboot PID; klog: $klog" >&2
    exit 1
fi
target_exec_line=$(grep -n "^<${target_pid}> EXEC /app0/eboot\.bin " "$klog" | \
    tail -n 1 | cut -d: -f1)
sed -n "${target_exec_line},\$p" "$klog" >"$target_klog"
target_pid_hex=$(printf '%x' "$target_pid")
if grep -Eq \
    "# proc ID: *${target_pid}$|mDBG: Sending signal\(pid: *${target_pid},|App Crash : PID=0x0*${target_pid_hex}([^0-9a-f]|$)|SYSTEM_XO_VIOLATION" \
    "$target_klog"; then
    kill_stale_process "$target_pid" || true
    sed -n '1,240p' "$target_klog" >&2
    echo "swapchain emitted PASS but its scoped kernel log is not clean: $target_klog" >&2
    exit 1
fi
baseline_warning='[KERNEL] WARNING: VM resource leak: set:1, res:0, amount:0x4000'
baseline_warning_count=$(grep -Fxc "$baseline_warning" "$target_klog" || true)
if grep -F '[KERNEL] WARNING:' "$target_klog" | \
       grep -Fvx "$baseline_warning" >/dev/null || \
   [ "$baseline_warning_count" -gt 1 ]; then
    kill_stale_process "$target_pid" || true
    echo "swapchain emitted PASS but its scoped kernel warnings exceed the proven FW 5.50 raw-ELF baseline: $target_klog" >&2
    exit 1
fi
kill_pair=$(sed -n \
    's/.*KillApp() appId={0x\([0-9A-Fa-f][0-9A-Fa-f]*\)} is requested from 0x\([0-9A-Fa-f][0-9A-Fa-f]*\).*/\1 \2/p' \
    "$target_klog" | tail -n 1)
kill_line=$(grep -n 'KillApp() appId=' "$target_klog" | tail -n 1 | cut -d: -f1 || true)
all_exited_line=$(grep -n '\[AppMgr\] All processes exited' "$target_klog" | \
    tail -n 1 | cut -d: -f1 || true)
if [ -z "$kill_pair" ] || [ -z "$kill_line" ] || \
   [ -z "$all_exited_line" ]; then
    kill_stale_process "$target_pid" || true
    echo "swapchain did not complete the kernel app-exit lifecycle; klog: $target_klog" >&2
    exit 1
fi
kill_app=${kill_pair%% *}
requester_app=${kill_pair#* }
if [ "$((0x$kill_app))" -ne "$((0x$requester_app))" ] || \
   [ "$all_exited_line" -le "$kill_line" ]; then
    kill_stale_process "$target_pid" || true
    echo "swapchain kernel app-exit lifecycle is inconsistent; klog: $target_klog" >&2
    exit 1
fi
if ! uv run --project "$pyps4debug_dir" python \
    "$script_dir/ps5debug_kill_process.py" --assert-absent \
    --pid "$target_pid" "$PS5_HOST" eboot.bin; then
    kill_stale_process "$target_pid" || true
    echo "swapchain process remained after PASS; klog: $target_klog" >&2
    exit 1
fi
if ! curl -sS --connect-timeout 3 --max-time 5 \
    "http://${PS5_HOST}:8080/" >/dev/null; then
    echo "swapchain passed but the post-run console probe failed; log: $log" >&2
    exit 1
fi

if [ "$baseline_warning_count" -eq 1 ]; then
    echo "FW550 swapchain: accepted proven raw-ELF baseline warning amount=0x4000"
fi
echo "FW550 ${qualification_label}: PASS (${pass_description}, clean exit and klog)"
echo "log: $log"
echo "klog: $target_klog"
