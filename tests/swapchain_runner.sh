#!/bin/sh
set -eu

repo_dir=$1
runner="$repo_dir/examples/run_fw550_swapchain.sh"
test_root=$(mktemp -d "${TMPDIR:-/tmp}/vulkan-ps5-runner.XXXXXX")
trap 'rm -rf "$test_root"' EXIT

mkdir -p "$test_root/bin" "$test_root/build" "$test_root/pyps4debug"
: >"$test_root/build/vulkan_ps5_swapchain_example.elf"
: >"$test_root/build/vulkan_ps5_process_cleanup.elf"

cat >"$test_root/bin/curl" <<'EOF'
#!/bin/sh
for arg do
    case "$arg" in
        */hbldr*)
            printf '%s\n' \
                'swapchain: 1800/1800 frames' \
                'swapchain: cleanup begin' \
                'swapchain: swapchain destroyed' \
                'swapchain: PASS 1800 frames'
            ;;
    esac
done
EOF

cat >"$test_root/bin/nc" <<'EOF'
#!/bin/sh
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
EOF

cat >"$test_root/bin/uv" <<'EOF'
#!/bin/sh
case " $* " in
    *" --pid 321 "*" eboot.bin "*) ;;
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
    VULKAN_PS5_KLOG_SETTLE_DELAY=0 \
    FAKE_UV_LOG="$test_root/uv.log" \
    FAKE_KLOG_MODE="$mode" \
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
