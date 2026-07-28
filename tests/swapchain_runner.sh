#!/bin/sh
set -eu

repo_dir=$1
runner="$repo_dir/examples/run_fw550_swapchain.sh"
test_root=$(mktemp -d "${TMPDIR:-/tmp}/vulkan-ps5-runner.XXXXXX")
trap 'rm -rf "$test_root"' EXIT

mkdir -p "$test_root/bin" "$test_root/build" "$test_root/pyps4debug"
: >"$test_root/build/vulkan_ps5_swapchain_example.elf"

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
printf '<321> EXEC /app0/eboot.bin [system], vm#1\n'
case "${FAKE_KLOG_MODE:-clean}" in
    clean) ;;
    crash)
        printf '%s\n' \
            '# A user thread receives a fatal signal' \
            '# signal: 11 (SIGSEGV)' \
            '# proc ID: 321'
        ;;
    *) exit 2 ;;
esac
EOF

cat >"$test_root/bin/uv" <<'EOF'
#!/bin/sh
case " $* " in
    *" --pid 321 "*) ;;
    *) echo "runner did not use exact launched PID" >&2; exit 2 ;;
esac
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
    FAKE_KLOG_MODE="$mode" \
        sh "$runner" >"$output" 2>&1
}

if ! run_runner clean "$test_root/clean.out" "$test_root/clean-logs"; then
    cat "$test_root/clean.out" >&2
    exit 1
fi
grep -F 'FW550 swapchain: PASS (1800 frames, clean exit and klog)' \
    "$test_root/clean.out" >/dev/null

if run_runner crash "$test_root/crash.out" "$test_root/crash-logs"; then
    echo "fatal PID-scoped klog unexpectedly passed" >&2
    exit 1
fi
grep -F 'scoped kernel log is not clean' "$test_root/crash.out" >/dev/null

echo "swapchain runner safety gate: PASS"
