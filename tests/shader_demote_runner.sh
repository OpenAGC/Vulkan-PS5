#!/bin/sh
set -eu

repo_dir=$1
runner="$repo_dir/examples/run_fw550_shader_demote.sh"
test_root=$(mktemp -d "${TMPDIR:-/tmp}/vulkan-ps5-shader-demote.XXXXXX")
trap 'rm -rf "$test_root"' EXIT

mkdir -p "$test_root/bin" "$test_root/build" "$test_root/pyps4debug"
: >"$test_root/build/vulkan_ps5_shader_demote_probe.elf"

cat >"$test_root/bin/curl" <<'EOF'
#!/bin/sh
for arg do
    case "$arg" in
        */hbldr*) printf '%s\n' \
            'shader_demote: PASS green=2048 blue=30720 demoted=32768 center=00000000' ;;
    esac
done
EOF

cat >"$test_root/bin/nc" <<'EOF'
#!/bin/sh
printf '%s\n' \
    '<971> EXEC /app0/eboot.bin [system], vm#1 category=native_game' \
    '<972> EXEC /app0/eboot.bin [system], vm#1 category=shell_ui'
printf '\000'
printf '%s\n' \
    '[SceLncService] KillApp() appId={0x00008018} is requested from 0x00008018' \
    '[AppMgr] All processes exited'
case "${FAKE_KLOG_MODE:-clean}" in
    clean) ;;
    crash) printf '%s\n' '# proc ID: 971' ;;
    *) exit 2 ;;
esac
EOF

cat >"$test_root/bin/uv" <<'EOF'
#!/bin/sh
case " $* " in
    *" --pid 971 "*" eboot.bin "*) ;;
    *) echo "shader-demote runner did not use exact PID" >&2; exit 2 ;;
esac
printf '%s\n' "$*" >>"$FAKE_UV_LOG"
echo "ps5debug-NG: no process matching pid 971"
EOF
chmod +x "$test_root/bin/curl" "$test_root/bin/nc" "$test_root/bin/uv"

run_runner() {
    mode=$1
    output=$2
    : >"$test_root/uv.log"
    PATH="$test_root/bin:$PATH" \
    PS5_HOST=127.0.0.1 \
    PYPS4DEBUG_DIR="$test_root/pyps4debug" \
    VULKAN_PS5_PROSPERO_BUILD="$test_root/build" \
    VULKAN_PS5_FW550_LOG_DIR="$test_root/logs-$mode" \
    VULKAN_PS5_KLOG_SETTLE_DELAY=0 \
    FAKE_UV_LOG="$test_root/uv.log" \
    FAKE_KLOG_MODE="$mode" \
        sh "$runner" >"$output" 2>&1
}

run_runner clean "$test_root/clean.out"
grep -F 'FW550 shader-demote probe: CLEAN' "$test_root/clean.out" >/dev/null
grep -F -- '--assert-absent --pid 971' "$test_root/uv.log" >/dev/null
if grep -F -- '--pid 972' "$test_root/uv.log" >/dev/null; then
    echo "shader-demote runner targeted the later ShellUI PID" >&2
    exit 1
fi

if run_runner crash "$test_root/crash.out"; then
    echo "fatal shader-demote klog unexpectedly passed" >&2
    exit 1
fi
grep -F 'fatal lifecycle fault' "$test_root/crash.out" >/dev/null
grep -F -- '--pid 971' "$test_root/uv.log" >/dev/null

echo "shader-demote runner safety gate: PASS"
