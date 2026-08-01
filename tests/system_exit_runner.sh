#!/bin/sh
set -eu

repo_dir=$1
runner="$repo_dir/examples/run_fw550_system_exit_probe.sh"
package_runner="$repo_dir/examples/run_fw550_package_consumer.sh"
test_root=$(mktemp -d "${TMPDIR:-/tmp}/vulkan-ps5-exit-probe.XXXXXX")
trap 'rm -rf "$test_root"' EXIT

mkdir -p "$test_root/bin" "$test_root/build" "$test_root/pyps4debug"
: >"$test_root/build/vulkan_ps5_system_exit_probe.elf"
: >"$test_root/build/vulkan_ps5_package_consumer.elf"

cat >"$test_root/bin/curl" <<'EOF'
#!/bin/sh
for arg do
    case "$arg" in
        */hbldr*)
            printf '%s\n' \
                'system-exit-probe: ready app=0x2016' \
                'package-consumer: PASS result=0'
            if [ "${FAKE_OPENAGC_FAILURE:-0}" = 1 ]; then
                printf '%s\n' \
                    'vulkan-ps5: OpenAGC agcDestroyDevice failed: 0x80890008 (device destruction requires all child objects to be destroyed)'
            fi
            ;;
    esac
done
EOF

cat >"$test_root/bin/nc" <<'EOF'
#!/bin/sh
printf '%s\n' \
    '<321> EXEC /app0/eboot.bin [system], vm#1 category=native_game' \
    '<322> EXEC /app0/eboot.bin [system], vm#1 category=shell_ui' \
    '[SceLncService] KillApp() appId={0x00002016} is requested from 0x00002016'
printf '\000'
case "${FAKE_KLOG_MODE:-baseline}" in
    clean) ;;
    duplicate)
        printf '%s\n' \
            '[SceLncService] KillApp() appId={0x00002016} is requested from 0x00000000'
        ;;
    baseline)
        printf '%s\n' \
            '[KERNEL] WARNING: VM resource leak: set:1, res:0, amount:0x4000'
        ;;
    crash) printf '%s\n' '# proc ID: 321' ;;
    *) exit 2 ;;
esac
printf '%s\n' '[AppMgr] All processes exited'
EOF

cat >"$test_root/bin/uv" <<'EOF'
#!/bin/sh
case " $* " in
    *" --pid 321 "*" eboot.bin "*) ;;
    *) echo "probe runner did not use exact PID" >&2; exit 2 ;;
esac
echo "ps5debug-NG: no process matching pid 321"
EOF
chmod +x "$test_root/bin/curl" "$test_root/bin/nc" "$test_root/bin/uv"

run_probe() {
    mode=$1
    output=$2
    openagc_failure=${3:-0}
    PATH="$test_root/bin:$PATH" \
    PS5_HOST=127.0.0.1 \
    PYPS4DEBUG_DIR="$test_root/pyps4debug" \
    VULKAN_PS5_PROSPERO_BUILD="$test_root/build" \
    VULKAN_PS5_FW550_LOG_DIR="$test_root/logs-$mode" \
    VULKAN_PS5_KLOG_SETTLE_DELAY=0 \
    FAKE_KLOG_MODE="$mode" \
    FAKE_OPENAGC_FAILURE="$openagc_failure" \
        sh "$runner" >"$output" 2>&1
}

run_package() {
    output=$1
    PATH="$test_root/bin:$PATH" \
    PS5_HOST=127.0.0.1 \
    PYPS4DEBUG_DIR="$test_root/pyps4debug" \
    VULKAN_PS5_PROSPERO_BUILD="$test_root/build" \
    VULKAN_PS5_FW550_LOG_DIR="$test_root/logs-package" \
    VULKAN_PS5_KLOG_SETTLE_DELAY=0 \
    FAKE_KLOG_MODE=baseline \
        sh "$package_runner" >"$output" 2>&1
}

run_probe baseline "$test_root/baseline.out"
grep -F 'FW550 system-exit probe: BASELINE_VM_WARNING amount=0x4000' \
    "$test_root/baseline.out" >/dev/null
run_probe clean "$test_root/clean.out"
grep -F 'FW550 system-exit probe: CLEAN' "$test_root/clean.out" >/dev/null
run_probe duplicate "$test_root/duplicate.out"
grep -F 'FW550 system-exit probe: CLEAN' "$test_root/duplicate.out" >/dev/null
if run_probe clean "$test_root/openagc-failure.out" 1; then
    echo "OpenAGC lifecycle failure unexpectedly passed" >&2
    exit 1
fi
grep -F 'did not reach its self-kill oracle' \
    "$test_root/openagc-failure.out" >/dev/null
if run_probe crash "$test_root/crash.out"; then
    echo "fatal baseline probe unexpectedly passed" >&2
    exit 1
fi
grep -F 'fatal lifecycle fault' "$test_root/crash.out" >/dev/null
run_package "$test_root/package.out"
grep -F 'FW550 package consumer: BASELINE_VM_WARNING amount=0x4000' \
    "$test_root/package.out" >/dev/null

echo "system-exit probe runner safety gate: PASS"
