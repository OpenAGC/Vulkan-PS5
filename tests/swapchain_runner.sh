#!/bin/sh
set -eu

repo_dir=$1
runner="$repo_dir/examples/run_fw550_swapchain.sh"
test_root=$(mktemp -d "${TMPDIR:-/tmp}/vulkan-ps5-runner.XXXXXX")
trap 'rm -rf "$test_root"' EXIT

mkdir -p "$test_root/bin" "$test_root/build" "$test_root/pyps4debug" \
    "$test_root/remote"
: >"$test_root/build/vulkan_ps5_swapchain_example.elf"
: >"$test_root/build/vulkan_ps5_process_cleanup.elf"

cat >"$test_root/bin/curl" <<'EOF'
#!/bin/sh
output=
take_output=0
upload=
take_upload=0
last_arg=
for arg do
    last_arg=$arg
    if [ "$take_output" = 1 ]; then
        output=$arg
        take_output=0
        continue
    fi
    if [ "$take_upload" = 1 ]; then
        upload=$arg
        take_upload=0
        continue
    fi
    case "$arg" in
        -o) take_output=1 ;;
        -T) take_upload=1 ;;
        */hbldr*)
            case "$arg" in
                *vulkan_ps5_process_cleanup*) ;;
                *)
                    if [ -n "${FAKE_EVENT_LOG:-}" ]; then
                        printf '%s\n' hbldr >>"$FAKE_EVENT_LOG"
                    fi
                    ;;
            esac
            printf '%s\n' \
                'swapchain: 1800/1800 frames' \
                "Total Pipeline Count: ${FAKE_PIPELINE_COUNT:-3}" \
                'swapchain: cleanup begin' \
                'swapchain: swapchain destroyed' \
                'swapchain: PASS 1800 frames'
            ;;
    esac
done
remote_file="$FAKE_CURL_REMOTE/$(basename "$last_arg")"
if [ -n "$upload" ]; then
    cp "$upload" "$remote_file"
fi
if [ -n "$output" ]; then
    if [ -f "$remote_file" ]; then
        cp "$remote_file" "$output"
    else
        : >"$output"
    fi
fi
EOF

cat >"$test_root/bin/nc" <<'EOF'
#!/bin/sh
if [ -n "${FAKE_EVENT_LOG:-}" ]; then
    printf '%s\n' nc >>"$FAKE_EVENT_LOG"
fi
if [ -n "${FAKE_NC_PID_FILE:-}" ]; then
    printf '%s\n' "$$" >"$FAKE_NC_PID_FILE"
fi
if [ "${FAKE_NC_EXIT_IMMEDIATELY:-0}" = 1 ]; then
    exit 0
fi
if [ "${FAKE_NC_WAIT_FOR_HBLDR:-0}" = 1 ]; then
    attempts=0
    until grep -F hbldr "$FAKE_EVENT_LOG" >/dev/null; do
        attempts=$((attempts + 1))
        if [ "$attempts" -ge 5 ]; then
            exit 3
        fi
        sleep 1
    done
fi
printf '<321> EXEC /app0/eboot.bin [system], vm#1 category=native_game\n'
printf '\000'
printf '<322> EXEC /app0/eboot.bin [system], vm#1 category=shell_ui\n'
case "${FAKE_KLOG_MODE:-clean}" in
    clean)
        printf '%s\n' \
            '[SysCore] KillApp() appId={0x00002016} is requested from 0x00002016' \
            '[AppMgr] All processes exited' \
            '[KERNEL] WARNING: VM resource leak: set:1, res:0, amount:0x4000'
        ;;
    crash)
        printf '%s\n' \
            '[SysCore] KillApp() appId={0x00002016} is requested from 0x00002016' \
            '[AppMgr] All processes exited' \
            '# A user thread receives a fatal signal' \
            '# signal: 11 (SIGSEGV)' \
            '# proc ID: 321'
        ;;
    no_lifecycle) ;;
    unexpected_warning)
        printf '%s\n' \
            '[SysCore] KillApp() appId={0x00002016} is requested from 0x00002016' \
            '[AppMgr] All processes exited' \
            '[KERNEL] WARNING: VM resource leak: set:1, res:0, amount:0x8000'
        ;;
    *) exit 2 ;;
esac
if [ "${FAKE_NC_HOLD:-0}" = 1 ]; then
    trap 'exit 0' TERM INT
    while :; do
        sleep 1
    done
fi
EOF

cat >"$test_root/bin/uv" <<'EOF'
#!/bin/sh
case " $* " in
    *" --pid 321 "*" eboot.bin "*) ;;
    *" --assert-absent "*" eboot.bin "*) ;;
    *) echo "runner did not use exact launched PID" >&2; exit 2 ;;
esac
printf '%s\n' "$*" >>"$FAKE_UV_LOG"
echo "ps5debug-NG: no matching qualification process"
EOF
chmod +x "$test_root/bin/curl" "$test_root/bin/nc" "$test_root/bin/uv"

run_runner() {
    mode=$1
    output=$2
    log_dir=$3
    PATH="$test_root/bin:$PATH" \
    PS5_HOST=127.0.0.1 \
    PYPS4DEBUG_DIR="$test_root/pyps4debug" \
    VULKAN_PS5_PROSPERO_BUILD="$test_root/build" \
    VULKAN_PS5_FW550_LOG_DIR="$log_dir" \
    VULKAN_PS5_KLOG_SETTLE_DELAY="${FAKE_KLOG_SETTLE_DELAY:-0}" \
    VULKAN_PS5_CLEANUP_SETTLE_DELAY=0 \
    FAKE_UV_LOG="$test_root/uv.log" \
    FAKE_CURL_REMOTE="$test_root/remote" \
    FAKE_KLOG_MODE="$mode" \
    FAKE_EVENT_LOG="${FAKE_EVENT_LOG:-}" \
    FAKE_NC_PID_FILE="${FAKE_NC_PID_FILE:-}" \
    FAKE_NC_HOLD="${FAKE_NC_HOLD:-0}" \
    FAKE_NC_EXIT_IMMEDIATELY="${FAKE_NC_EXIT_IMMEDIATELY:-0}" \
    FAKE_NC_WAIT_FOR_HBLDR="${FAKE_NC_WAIT_FOR_HBLDR:-0}" \
    VULKAN_PS5_CONTINUOUS_KLOG="${FAKE_CONTINUOUS_KLOG:-0}" \
    VULKAN_PS5_LIVE_KLOG_START_DELAY="${FAKE_LIVE_KLOG_START_DELAY:-0}" \
    VULKAN_PS5_SWAPCHAIN_EXPECTED_SHA256="${FAKE_EXPECTED_SHA256:-e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855}" \
    VULKAN_PS5_CLEANUP_EXPECTED_SHA256="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" \
        sh "$runner" >"$output" 2>&1
}

if ! run_runner clean "$test_root/clean.out" "$test_root/clean-logs"; then
    cat "$test_root/clean.out" >&2
    exit 1
fi
grep -F 'FW550 swapchain: PASS (1800 frames, clean exit and klog)' \
    "$test_root/clean.out" >/dev/null
grep -F 'accepted proven raw-ELF baseline warning amount=0x4000' \
    "$test_root/clean.out" >/dev/null
grep -F -- '--assert-absent 127.0.0.1 eboot.bin' "$test_root/uv.log" >/dev/null
grep -F -- '--assert-absent --pid 321 127.0.0.1 eboot.bin' \
    "$test_root/uv.log" >/dev/null

: >"$test_root/events.log"
(
    FAKE_EVENT_LOG="$test_root/events.log" \
    FAKE_NC_PID_FILE="$test_root/nc.pid" \
    FAKE_NC_HOLD=1 \
    FAKE_NC_WAIT_FOR_HBLDR=1 \
    FAKE_KLOG_SETTLE_DELAY=1 \
    FAKE_CONTINUOUS_KLOG=1 \
        run_runner clean "$test_root/continuous.out" \
        "$test_root/continuous-logs"
)
if [ "$(sed -n '1p' "$test_root/events.log")" != nc ] || \
   [ "$(sed -n '2p' "$test_root/events.log")" != hbldr ]; then
    echo "continuous klog listener did not start before qualification launch" >&2
    cat "$test_root/events.log" >&2
    exit 1
fi
if [ "$(grep -Fxc nc "$test_root/events.log")" -ne 1 ]; then
    echo "continuous mode opened more than one kernel-log listener" >&2
    cat "$test_root/events.log" >&2
    exit 1
fi
if kill -0 "$(cat "$test_root/nc.pid")" 2>/dev/null; then
    echo "continuous klog listener remained after runner exit" >&2
    exit 1
fi
grep -F 'FW550 swapchain: PASS (1800 frames, clean exit and klog)' \
    "$test_root/continuous.out" >/dev/null

: >"$test_root/unavailable-events.log"
if (
    FAKE_EVENT_LOG="$test_root/unavailable-events.log" \
    FAKE_NC_EXIT_IMMEDIATELY=1 \
    FAKE_CONTINUOUS_KLOG=1 \
    FAKE_LIVE_KLOG_START_DELAY=1 \
        run_runner clean "$test_root/unavailable-listener.out" \
        "$test_root/unavailable-listener-logs"
); then
    echo "unavailable continuous klog listener unexpectedly launched" >&2
    exit 1
fi
grep -F 'continuous kernel-log listener was unavailable before launch' \
    "$test_root/unavailable-listener.out" >/dev/null
if grep -F hbldr "$test_root/unavailable-events.log" >/dev/null; then
    echo "qualification launched after continuous klog listener failure" >&2
    exit 1
fi

if (
    FAKE_CONTINUOUS_KLOG=invalid \
        run_runner clean "$test_root/invalid-continuous.out" \
        "$test_root/invalid-continuous-logs"
); then
    echo "invalid continuous klog mode unexpectedly passed" >&2
    exit 1
fi
grep -F 'VULKAN_PS5_CONTINUOUS_KLOG must be 0 or 1' \
    "$test_root/invalid-continuous.out" >/dev/null

if (
    VULKAN_PS5_LIVE_KLOG_TIMEOUT=0 \
        run_runner clean "$test_root/invalid-live-timeout.out" \
        "$test_root/invalid-live-timeout-logs"
); then
    echo "invalid live klog timeout unexpectedly passed" >&2
    exit 1
fi
grep -F 'VULKAN_PS5_LIVE_KLOG_TIMEOUT must be a positive integer' \
    "$test_root/invalid-live-timeout.out" >/dev/null

: >"$test_root/build/eden-bootstrap.elf"
if ! (
    VULKAN_PS5_QUALIFICATION_ELF="$test_root/build/eden-bootstrap.elf" \
        VULKAN_PS5_QUALIFICATION_REMOTE_NAME=eden_ps5_bootstrap \
        VULKAN_PS5_QUALIFICATION_LABEL=eden \
        VULKAN_PS5_QUALIFICATION_PASS_PATTERN='^swapchain: PASS 1800 frames$' \
        VULKAN_PS5_QUALIFICATION_REQUIRED_PATTERN='Total Pipeline Count: [1-9][0-9]*' \
        VULKAN_PS5_QUALIFICATION_REQUIRED_PATTERN_2='swapchain: cleanup begin' \
        VULKAN_PS5_QUALIFICATION_REQUIRED_PATTERN_3='swapchain: swapchain destroyed' \
        VULKAN_PS5_QUALIFICATION_REQUIRED_PATTERN_4='swapchain: 1800/1800 frames' \
        VULKAN_PS5_QUALIFICATION_PASS_DESCRIPTION='600 frames and compute oracle' \
        run_runner clean "$test_root/external.out" "$test_root/external-logs"
); then
    cat "$test_root/external.out" >&2
    exit 1
fi
grep -F 'FW550 eden: PASS (600 frames and compute oracle, clean exit and klog)' \
    "$test_root/external.out" >/dev/null

if (
    VULKAN_PS5_QUALIFICATION_REQUIRED_PATTERN='required diagnostic is absent' \
        run_runner clean "$test_root/missing-required.out" \
        "$test_root/missing-required-logs"
); then
    echo "missing required diagnostic oracle unexpectedly passed" >&2
    exit 1
fi
grep -F 'did not produce its required diagnostic oracle' \
    "$test_root/missing-required.out" >/dev/null

printf 'init\n' >"$test_root/build/eden.launch"
sidecar_sha=$(shasum -a 256 "$test_root/build/eden.launch" | awk '{print $1}')
printf 'representative-homebrew\n' >"$test_root/build/2048.nro"
asset_sha=$(shasum -a 256 "$test_root/build/2048.nro" | awk '{print $1}')
if ! (
    VULKAN_PS5_QUALIFICATION_SIDECAR="$test_root/build/eden.launch" \
        VULKAN_PS5_QUALIFICATION_SIDECAR_REMOTE_NAME=eden.launch \
        VULKAN_PS5_QUALIFICATION_SIDECAR_EXPECTED_SHA256="$sidecar_sha" \
        VULKAN_PS5_QUALIFICATION_ASSET="$test_root/build/2048.nro" \
        VULKAN_PS5_QUALIFICATION_ASSET_REMOTE_NAME=2048.nro \
        VULKAN_PS5_QUALIFICATION_ASSET_EXPECTED_SHA256="$asset_sha" \
        run_runner clean "$test_root/sidecar.out" "$test_root/sidecar-logs"
); then
    cat "$test_root/sidecar.out" >&2
    exit 1
fi
cmp "$test_root/build/eden.launch" "$test_root/remote/eden.launch"
cmp "$test_root/build/2048.nro" "$test_root/remote/2048.nro"

if (
    VULKAN_PS5_QUALIFICATION_SIDECAR="$test_root/build/missing.launch" \
        VULKAN_PS5_QUALIFICATION_SIDECAR_REMOTE_NAME=eden.launch \
        VULKAN_PS5_QUALIFICATION_SIDECAR_EXPECTED_SHA256="$sidecar_sha" \
        run_runner clean "$test_root/missing-sidecar.out" "$test_root/missing-sidecar-logs"
); then
    echo "missing qualification sidecar unexpectedly passed" >&2
    exit 1
fi
grep -F 'missing qualification sidecar' "$test_root/missing-sidecar.out" >/dev/null

if (
    VULKAN_PS5_QUALIFICATION_SIDECAR="$test_root/build/eden.launch" \
        VULKAN_PS5_QUALIFICATION_SIDECAR_REMOTE_NAME=eden.launch \
        VULKAN_PS5_QUALIFICATION_SIDECAR_EXPECTED_SHA256=0000000000000000000000000000000000000000000000000000000000000000 \
        run_runner clean "$test_root/wrong-sidecar-hash.out" \
        "$test_root/wrong-sidecar-hash-logs"
); then
    echo "wrong qualification sidecar hash unexpectedly passed" >&2
    exit 1
fi
grep -F 'qualification sidecar SHA-256 mismatch' \
    "$test_root/wrong-sidecar-hash.out" >/dev/null

if (
    VULKAN_PS5_QUALIFICATION_ASSET="$test_root/build/2048.nro" \
        VULKAN_PS5_QUALIFICATION_ASSET_REMOTE_NAME=2048.nro \
        VULKAN_PS5_QUALIFICATION_ASSET_EXPECTED_SHA256=0000000000000000000000000000000000000000000000000000000000000000 \
        run_runner clean "$test_root/wrong-asset-hash.out" \
        "$test_root/wrong-asset-hash-logs"
); then
    echo "wrong qualification asset hash unexpectedly passed" >&2
    exit 1
fi
grep -F 'qualification asset SHA-256 mismatch' \
    "$test_root/wrong-asset-hash.out" >/dev/null

if (
    VULKAN_PS5_QUALIFICATION_REMOTE_NAME='../unsafe' \
        run_runner clean "$test_root/unsafe-name.out" \
        "$test_root/unsafe-name-logs"
); then
    echo "unsafe qualification remote name unexpectedly passed" >&2
    exit 1
fi
grep -F 'qualification remote name must use' \
    "$test_root/unsafe-name.out" >/dev/null

if FAKE_EXPECTED_SHA256=0000000000000000000000000000000000000000000000000000000000000000 \
    run_runner clean "$test_root/wrong-hash.out" \
    "$test_root/wrong-hash-logs"; then
    echo "wrong swapchain ELF hash unexpectedly passed" >&2
    exit 1
fi
grep -F 'swapchain ELF SHA-256 mismatch' "$test_root/wrong-hash.out" >/dev/null
unset FAKE_EXPECTED_SHA256

mv "$test_root/build/vulkan_ps5_process_cleanup.elf" \
    "$test_root/build/vulkan_ps5_process_cleanup.elf.missing"
if run_runner clean "$test_root/missing-cleanup.out" \
    "$test_root/missing-cleanup-logs"; then
    echo "missing cleanup prerequisite unexpectedly passed" >&2
    exit 1
fi
grep -F 'missing Prospero cleanup prerequisite' \
    "$test_root/missing-cleanup.out" >/dev/null
mv "$test_root/build/vulkan_ps5_process_cleanup.elf.missing" \
    "$test_root/build/vulkan_ps5_process_cleanup.elf"

: >"$test_root/uv.log"
if run_runner crash "$test_root/crash.out" "$test_root/crash-logs"; then
    echo "fatal PID-scoped klog unexpectedly passed" >&2
    exit 1
fi
grep -F 'scoped kernel log is not clean' "$test_root/crash.out" >/dev/null
grep -F -- '--pid 321' "$test_root/uv.log" >/dev/null

: >"$test_root/uv.log"
if run_runner unexpected_warning "$test_root/unexpected-warning.out" \
    "$test_root/unexpected-warning-logs"; then
    echo "unexpected kernel warning passed the baseline gate" >&2
    exit 1
fi
grep -F 'warnings exceed the proven FW 5.50 raw-ELF baseline' \
    "$test_root/unexpected-warning.out" >/dev/null
grep -F -- '--pid 321' "$test_root/uv.log" >/dev/null

: >"$test_root/uv.log"
if run_runner no_lifecycle "$test_root/no-lifecycle.out" \
    "$test_root/no-lifecycle-logs"; then
    echo "missing kernel app-exit lifecycle unexpectedly passed" >&2
    exit 1
fi
grep -F 'did not complete the kernel app-exit lifecycle' \
    "$test_root/no-lifecycle.out" >/dev/null
grep -F -- '--pid 321' "$test_root/uv.log" >/dev/null

echo "swapchain runner safety gate: PASS"
