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
continuous_klog=${VULKAN_PS5_CONTINUOUS_KLOG:-0}
live_klog_timeout=${VULKAN_PS5_LIVE_KLOG_TIMEOUT:-180}
live_klog_start_delay=${VULKAN_PS5_LIVE_KLOG_START_DELAY:-1}
cleanup_settle_delay=${VULKAN_PS5_CLEANUP_SETTLE_DELAY:-2}
absence_check_count=${VULKAN_PS5_ABSENCE_CHECK_COUNT:-1}
absence_check_delay=${VULKAN_PS5_ABSENCE_CHECK_DELAY:-1}
pyps4debug_dir=${PYPS4DEBUG_DIR:-/Users/bizkut/Downloads/PS5/homebrew/PyPS4debug}
elf=${VULKAN_PS5_QUALIFICATION_ELF:-$build_dir/vulkan_ps5_swapchain_example.elf}
cleanup_elf=${VULKAN_PS5_CLEANUP_ELF:-$build_dir/vulkan_ps5_process_cleanup.elf}
remote_name=${VULKAN_PS5_QUALIFICATION_REMOTE_NAME:-vulkan_ps5_swapchain}
qualification_label=${VULKAN_PS5_QUALIFICATION_LABEL:-swapchain}
pass_pattern=${VULKAN_PS5_QUALIFICATION_PASS_PATTERN:-'^swapchain: PASS 1800 frames$'}
pass_description=${VULKAN_PS5_QUALIFICATION_PASS_DESCRIPTION:-'1800 frames'}
required_pattern=${VULKAN_PS5_QUALIFICATION_REQUIRED_PATTERN:-}
required_pattern_2=${VULKAN_PS5_QUALIFICATION_REQUIRED_PATTERN_2:-}
required_pattern_3=${VULKAN_PS5_QUALIFICATION_REQUIRED_PATTERN_3:-}
required_pattern_4=${VULKAN_PS5_QUALIFICATION_REQUIRED_PATTERN_4:-}
required_pattern_5=${VULKAN_PS5_QUALIFICATION_REQUIRED_PATTERN_5:-}
required_pattern_6=${VULKAN_PS5_QUALIFICATION_REQUIRED_PATTERN_6:-}
required_pattern_7=${VULKAN_PS5_QUALIFICATION_REQUIRED_PATTERN_7:-}
required_pattern_8=${VULKAN_PS5_QUALIFICATION_REQUIRED_PATTERN_8:-}
required_pattern_9=${VULKAN_PS5_QUALIFICATION_REQUIRED_PATTERN_9:-}
reject_pattern=${VULKAN_PS5_QUALIFICATION_REJECT_PATTERN:-}
expected_sha256=${VULKAN_PS5_SWAPCHAIN_EXPECTED_SHA256:-}
expected_cleanup_sha256=${VULKAN_PS5_CLEANUP_EXPECTED_SHA256:-}
sidecar=${VULKAN_PS5_QUALIFICATION_SIDECAR:-}
sidecar_remote_name=${VULKAN_PS5_QUALIFICATION_SIDECAR_REMOTE_NAME:-}
expected_sidecar_sha256=${VULKAN_PS5_QUALIFICATION_SIDECAR_EXPECTED_SHA256:-}
asset=${VULKAN_PS5_QUALIFICATION_ASSET:-}
asset_remote_name=${VULKAN_PS5_QUALIFICATION_ASSET_REMOTE_NAME:-}
expected_asset_sha256=${VULKAN_PS5_QUALIFICATION_ASSET_EXPECTED_SHA256:-}
forbidden_fixed_va_sha256=b3122a9a6137a99985651f84a424577139ad1676322650fa1c972957d2a8d2a1

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
if [ -n "$asset" ]; then
    if [ ! -f "$asset" ]; then
        echo "missing qualification asset: $asset" >&2
        exit 2
    fi
    case "$asset_remote_name" in
        ''|*[!A-Za-z0-9_.-]*)
            echo "qualification asset remote name must use A-Z, a-z, 0-9, _, ., or -" >&2
            exit 2
            ;;
    esac
elif [ -n "$asset_remote_name" ] || [ -n "$expected_asset_sha256" ]; then
    echo "qualification asset path is required with asset metadata" >&2
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
if [ "${#required_pattern}" -gt 256 ] || \
   [ "${#required_pattern_2}" -gt 256 ] || \
   [ "${#required_pattern_3}" -gt 256 ] || \
   [ "${#required_pattern_4}" -gt 256 ] || \
   [ "${#required_pattern_5}" -gt 256 ] || \
   [ "${#required_pattern_6}" -gt 256 ] || \
   [ "${#required_pattern_7}" -gt 256 ] || \
   [ "${#required_pattern_8}" -gt 256 ] || \
   [ "${#required_pattern_9}" -gt 256 ] || \
   [ "${#reject_pattern}" -gt 256 ]; then
    echo "qualification required or reject pattern is oversized" >&2
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
case "$continuous_klog" in
    0|1) ;;
    *)
        echo "VULKAN_PS5_CONTINUOUS_KLOG must be 0 or 1" >&2
        exit 2
        ;;
esac
case "$live_klog_timeout" in
    ''|*[!0-9]*|0)
        echo "VULKAN_PS5_LIVE_KLOG_TIMEOUT must be a positive integer" >&2
        exit 2
        ;;
esac
case "$live_klog_start_delay" in
    ''|*[!0-9]*)
        echo "VULKAN_PS5_LIVE_KLOG_START_DELAY must be a non-negative integer" >&2
        exit 2
        ;;
esac
case "$cleanup_settle_delay" in
    ''|*[!0-9]*)
        echo "VULKAN_PS5_CLEANUP_SETTLE_DELAY must be a non-negative integer" >&2
        exit 2
        ;;
esac
case "$absence_check_count" in
    1|2|3) ;;
    *)
        echo "VULKAN_PS5_ABSENCE_CHECK_COUNT must be 1, 2, or 3" >&2
        exit 2
        ;;
esac
case "$absence_check_delay" in
    ''|*[!0-9]*)
        echo "VULKAN_PS5_ABSENCE_CHECK_DELAY must be a non-negative integer" >&2
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

reject_forbidden_qualification_elf() {
    actual=$(shasum -a 256 "$elf" | awk '{print $1}')
    if [ "$actual" = "$forbidden_fixed_va_sha256" ]; then
        echo "refusing rejected fixed-address ELF: sha256=$actual" >&2
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

reject_forbidden_qualification_elf
verify_local_sha256 "$elf" "$expected_sha256" 'swapchain ELF'
verify_local_sha256 "$cleanup_elf" "$expected_cleanup_sha256" \
    'cleanup prerequisite'
if [ -n "$sidecar" ]; then
    verify_local_sha256 "$sidecar" "$expected_sidecar_sha256" \
        'qualification sidecar'
fi
if [ -n "$asset" ]; then
    verify_local_sha256 "$asset" "$expected_asset_sha256" \
        'qualification asset'
fi
if ! curl -sS --connect-timeout 3 --max-time 5 \
    "http://${PS5_HOST}:8080/" >/dev/null; then
    echo "FW 5.50 websrv is unreachable at ${PS5_HOST}:8080" >&2
    exit 1
fi

cleanup_dir=/data/homebrew/vulkan_ps5_process_cleanup
target_pid=
live_klog_pid=

assert_no_eboot_process() {
    uv run --project "$pyps4debug_dir" python \
        "$script_dir/ps5debug_kill_process.py" --assert-absent \
        "$PS5_HOST" eboot.bin
}

assert_no_scoped_eboot_process() {
    uv run --project "$pyps4debug_dir" python \
        "$script_dir/ps5debug_kill_process.py" --assert-absent \
        --pid "$1" "$PS5_HOST" eboot.bin
}

assert_no_eboot_process_repeated() {
    check=1
    while [ "$check" -le "$absence_check_count" ]; do
        assert_no_eboot_process || return 1
        if [ "$check" -lt "$absence_check_count" ]; then
            sleep "$absence_check_delay"
        fi
        check=$((check + 1))
    done
}

assert_no_scoped_eboot_process_repeated() {
    scoped_pid=$1
    check=1
    while [ "$check" -le "$absence_check_count" ]; do
        assert_no_scoped_eboot_process "$scoped_pid" || return 1
        if [ "$check" -lt "$absence_check_count" ]; then
            sleep "$absence_check_delay"
        fi
        check=$((check + 1))
    done
}

kill_stale_process() {
    target_pid_to_kill=$1
    uv run --project "$pyps4debug_dir" python \
        "$script_dir/ps5debug_kill_process.py" --pid "$target_pid_to_kill" \
        "$PS5_HOST" eboot.bin
}

kill_named_eboot_process() {
    uv run --project "$pyps4debug_dir" python \
        "$script_dir/ps5debug_kill_process.py" "$PS5_HOST" eboot.bin
}

launch_pinned_cleanup() {
    if ! curl -sS --connect-timeout 3 --max-time 10 \
        "http://${PS5_HOST}:8080/hbldr?pipe=0&daemon=1&path=${cleanup_dir}/eboot.elf" \
        >/dev/null; then
        echo "pinned cleanup launch failed" >&2
        return 1
    fi
    sleep "$cleanup_settle_delay"
    if ! curl -sS --connect-timeout 3 --max-time 5 \
        "http://${PS5_HOST}:8080/" >/dev/null; then
        echo "pinned cleanup left websrv unreachable" >&2
        return 1
    fi
}

finalize_process_state() {
    status=$1
    cleanup_failed=0
    trap - 0

    if [ "$status" -ne 0 ]; then
        echo "qualification failed; relaunching pinned cleanup before exit checks" >&2
        if ! launch_pinned_cleanup; then
            cleanup_failed=1
        fi
    fi

    if [ -n "$target_pid" ] &&
       ! assert_no_scoped_eboot_process_repeated "$target_pid"; then
        kill_stale_process "$target_pid" || true
        if ! assert_no_scoped_eboot_process_repeated "$target_pid"; then
            echo "qualification PID $target_pid remained at exit" >&2
            cleanup_failed=1
        fi
    fi

    if ! assert_no_eboot_process_repeated; then
        echo "an exact eboot.bin process remained at exit; attempting cleanup" >&2
        kill_named_eboot_process || true
        if ! assert_no_eboot_process_repeated; then
            echo "an exact eboot.bin process remained after exit cleanup" >&2
            cleanup_failed=1
        fi
    fi

    if [ "$status" -eq 0 ] && [ "$cleanup_failed" -ne 0 ]; then
        exit 1
    fi
    exit "$status"
}

stop_continuous_klog() {
    if [ -n "$live_klog_pid" ]; then
        if kill -0 "$live_klog_pid" 2>/dev/null; then
            kill "$live_klog_pid" 2>/dev/null || true
        fi
        wait "$live_klog_pid" 2>/dev/null || true
        live_klog_pid=
    fi
}

finalize_runner() {
    status=$1
    stop_continuous_klog
    finalize_process_state "$status"
}

curl -sS --connect-timeout 3 --max-time 30 \
    "ftp://${PS5_HOST}:2121/" --quote "MKD $cleanup_dir" \
    >/dev/null 2>&1 || true
curl -sS --connect-timeout 3 --max-time 30 -T "$cleanup_elf" \
    "ftp://${PS5_HOST}:2121${cleanup_dir}/eboot.elf" >/dev/null
verify_remote_sha256 \
    "ftp://${PS5_HOST}:2121${cleanup_dir}/eboot.elf" \
    "$expected_cleanup_sha256" 'cleanup prerequisite'
trap 'finalize_runner "$?"' 0
launch_pinned_cleanup
if ! assert_no_eboot_process_repeated; then
    echo "cleanup prerequisite did not retire every exact eboot.bin process" >&2
    exit 1
fi

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
start_continuous_klog() {
    : >"$klog"
    : >"$klog.capture.err"
    nc -w "$live_klog_timeout" "$PS5_HOST" "$klog_port" \
        >"$klog" 2>"$klog.capture.err" &
    live_klog_pid=$!
    sleep "$live_klog_start_delay"
    if ! kill -0 "$live_klog_pid" 2>/dev/null; then
        wait "$live_klog_pid" 2>/dev/null || true
        live_klog_pid=
        echo "continuous kernel-log listener was unavailable before launch" >&2
        return 1
    fi
}

finish_klog_capture() {
    sleep "$klog_settle_delay"
    if [ "$continuous_klog" -eq 1 ]; then
        stop_continuous_klog
    else
        nc -w 5 "$PS5_HOST" "$klog_port" >"$klog" 2>&1
    fi
    if [ ! -s "$klog" ]; then
        echo "kernel-log capture is empty: $klog" >&2
        return 1
    fi
    sanitize_klog "$klog"
}

if [ "$continuous_klog" -eq 1 ]; then
    start_continuous_klog
fi

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
    if [ -n "$asset" ]; then
        curl -sS --connect-timeout 3 --max-time 30 -T "$asset" \
            "ftp://${PS5_HOST}:2121${remote_dir}/${asset_remote_name}" >/dev/null
        verify_remote_sha256 \
            "ftp://${PS5_HOST}:2121${remote_dir}/${asset_remote_name}" \
            "$expected_asset_sha256" 'qualification asset'
    fi
    curl -sS --connect-timeout 3 --max-time "$websrv_timeout" \
        "http://${PS5_HOST}:8080/hbldr?pipe=1&daemon=0&path=${remote_dir}/eboot.elf"
} >"$log" 2>&1; then
    finish_klog_capture || true
    if [ -s "$klog" ]; then
        target_pid=$(latest_eboot_pid "$klog")
    fi
    sed -n '1,200p' "$log" >&2
    if [ -s "$klog" ]; then
        sed -n '1,240p' "$klog" >&2
    fi
    echo "swapchain run failed; log: $log" >&2
    exit 1
fi
if ! finish_klog_capture; then
    echo "swapchain output completed but klog could not be verified" >&2
    exit 1
fi
sed -n '1,200p' "$log"
if ! grep -E "$pass_pattern" "$log" >/dev/null; then
    target_pid=$(latest_eboot_pid "$klog")
    echo "swapchain run did not produce its PASS oracle; log: $log" >&2
    exit 1
fi
if [ -n "$required_pattern" ] && ! grep -E "$required_pattern" "$log" >/dev/null; then
    target_pid=$(latest_eboot_pid "$klog")
    echo "swapchain run did not produce its required diagnostic oracle; log: $log" >&2
    exit 1
fi
if [ -n "$required_pattern_2" ] && ! grep -E "$required_pattern_2" "$log" >/dev/null; then
    target_pid=$(latest_eboot_pid "$klog")
    echo "swapchain run did not produce its second required diagnostic oracle; log: $log" >&2
    exit 1
fi
if [ -n "$required_pattern_3" ] && ! grep -E "$required_pattern_3" "$log" >/dev/null; then
    target_pid=$(latest_eboot_pid "$klog")
    echo "swapchain run did not produce its third required diagnostic oracle; log: $log" >&2
    exit 1
fi
if [ -n "$required_pattern_4" ] && ! grep -E "$required_pattern_4" "$log" >/dev/null; then
    target_pid=$(latest_eboot_pid "$klog")
    echo "swapchain run did not produce its fourth required diagnostic oracle; log: $log" >&2
    exit 1
fi
if [ -n "$required_pattern_5" ] && ! grep -E "$required_pattern_5" "$log" >/dev/null; then
    target_pid=$(latest_eboot_pid "$klog")
    echo "swapchain run did not produce its fifth required diagnostic oracle; log: $log" >&2
    exit 1
fi
if [ -n "$required_pattern_6" ] && ! grep -E "$required_pattern_6" "$log" >/dev/null; then
    target_pid=$(latest_eboot_pid "$klog")
    echo "swapchain run did not produce its sixth required diagnostic oracle; log: $log" >&2
    exit 1
fi
if [ -n "$required_pattern_7" ] && ! grep -E "$required_pattern_7" "$log" >/dev/null; then
    target_pid=$(latest_eboot_pid "$klog")
    echo "swapchain run did not produce its seventh required diagnostic oracle; log: $log" >&2
    exit 1
fi
if [ -n "$required_pattern_8" ] && ! grep -E "$required_pattern_8" "$log" >/dev/null; then
    target_pid=$(latest_eboot_pid "$klog")
    echo "swapchain run did not produce its eighth required diagnostic oracle; log: $log" >&2
    exit 1
fi
if [ -n "$required_pattern_9" ] && ! grep -E "$required_pattern_9" "$log" >/dev/null; then
    target_pid=$(latest_eboot_pid "$klog")
    echo "swapchain run did not produce its ninth required diagnostic oracle; log: $log" >&2
    exit 1
fi
if [ -n "$reject_pattern" ]; then
    if grep -E "$reject_pattern" "$log" >/dev/null; then
        target_pid=$(latest_eboot_pid "$klog")
        echo "swapchain application log matched its reject oracle; log: $log" >&2
        exit 1
    else
        reject_status=$?
        if [ "$reject_status" -gt 1 ]; then
            echo "qualification reject pattern is not a valid extended regular expression" >&2
            exit 2
        fi
    fi
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
if ! assert_no_scoped_eboot_process_repeated "$target_pid"; then
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
