#!/bin/sh
set -eu

: "${PS5_HOST:?set PS5_HOST to the FW 5.50 console address}"

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(dirname -- "$script_dir")
build_dir=${VULKAN_PS5_PROSPERO_BUILD:-$repo_dir/build-prospero-m2}
run_count=${VULKAN_PS5_FW550_RUNS:-2}
log_dir=${VULKAN_PS5_FW550_LOG_DIR:-$script_dir/qualification-logs}

case "$run_count" in
    ''|*[!0-9]*|0)
        echo "VULKAN_PS5_FW550_RUNS must be a positive integer" >&2
        exit 2
        ;;
esac

compute_elf="$build_dir/vulkan_ps5_compute_example.elf"
triangle_elf="$build_dir/vulkan_ps5_triangle_example.elf"
for elf in "$compute_elf" "$triangle_elf"; do
    if [ ! -f "$elf" ]; then
        echo "missing Prospero sample: $elf" >&2
        exit 2
    fi
done

if ! curl -sS --connect-timeout 3 --max-time 5 \
    "http://${PS5_HOST}:8080/" >/dev/null; then
    echo "FW 5.50 websrv is unreachable at ${PS5_HOST}:8080" >&2
    exit 1
fi

mkdir -p "$log_dir"
timestamp=$(date -u +%Y%m%dT%H%M%SZ)

run_sample()
{
    sample=$1
    elf=$2
    expected=$3
    run=1
    while [ "$run" -le "$run_count" ]; do
        log="$log_dir/${timestamp}-${sample}-run${run}.log"
        echo "FW550 $sample run $run/$run_count"
        if ! "$script_dir/deploy_websrv.sh" "$elf" \
            "vulkan_ps5_${sample}" >"$log" 2>&1; then
            sed -n '1,160p' "$log" >&2
            echo "$sample run $run failed; log: $log" >&2
            exit 1
        fi
        sed -n '1,160p' "$log"
        if ! grep -E "$expected" "$log" >/dev/null; then
            echo "$sample run $run did not produce its PASS oracle; log: $log" >&2
            exit 1
        fi
        run=$((run + 1))
    done
}

run_sample compute "$compute_elf" '^compute: PASS 1024 deterministic values$'
run_sample triangle "$triangle_elf" '^triangle: PASS [0-9]+ green pixels$'

echo "FW550 Milestone 3: PASS (${run_count} compute + ${run_count} triangle runs)"
echo "logs: $log_dir"
