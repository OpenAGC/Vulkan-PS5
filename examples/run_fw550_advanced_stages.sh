#!/bin/sh
set -eu

: "${PS5_HOST:?set PS5_HOST to the FW 5.50 console address}"

if [ "$#" -ne 1 ]; then
    echo "usage: $0 geometry|tessellation" >&2
    exit 2
fi

stage=$1
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(dirname -- "$script_dir")
build_dir=${VULKAN_PS5_PROSPERO_BUILD:-$repo_dir/build-prospero-m2}
run_count=${VULKAN_PS5_FW550_RUNS:-1}
log_dir=${VULKAN_PS5_FW550_LOG_DIR:-$script_dir/qualification-logs}
websrv_timeout=${VULKAN_PS5_WEBSRV_TIMEOUT:-60}

case "$run_count" in
    ''|*[!0-9]*|0)
        echo "VULKAN_PS5_FW550_RUNS must be a positive integer" >&2
        exit 2
        ;;
esac

case "$stage" in
    geometry)
        elf="$build_dir/vulkan_ps5_geometry_example.elf"
        expected='^geometry: PASS [0-9]+ green pixels$'
        ;;
    tessellation)
        elf="$build_dir/vulkan_ps5_tessellation_example.elf"
        expected='^tessellation: PASS [0-9]+ green pixels$'
        ;;
    *)
        echo "usage: $0 geometry|tessellation" >&2
        exit 2
        ;;
esac

if [ ! -f "$elf" ]; then
    echo "missing Prospero sample: $elf" >&2
    exit 2
fi
if ! curl -sS --connect-timeout 3 --max-time 5 \
    "http://${PS5_HOST}:8080/" >/dev/null; then
    echo "FW 5.50 websrv is unreachable at ${PS5_HOST}:8080" >&2
    exit 1
fi

mkdir -p "$log_dir"
timestamp=$(date -u +%Y%m%dT%H%M%SZ)
run=1
while [ "$run" -le "$run_count" ]; do
    log="$log_dir/${timestamp}-${stage}-run${run}.log"
    echo "FW550 $stage run $run/$run_count"
    if ! VULKAN_PS5_WEBSRV_TIMEOUT="$websrv_timeout" \
        "$script_dir/deploy_websrv.sh" "$elf" \
        "vulkan_ps5_${stage}" >"$log" 2>&1; then
        sed -n '1,160p' "$log" >&2
        echo "$stage run $run failed; log: $log" >&2
        exit 1
    fi
    sed -n '1,160p' "$log"
    if ! grep -E "$expected" "$log" >/dev/null; then
        echo "$stage run $run did not produce its PASS oracle; log: $log" >&2
        exit 1
    fi
    run=$((run + 1))
done

echo "FW550 advanced stage: PASS ($stage, $run_count run(s))"
echo "logs: $log_dir"
