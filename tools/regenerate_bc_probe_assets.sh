#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(dirname -- "$script_dir")
psbc_root=${OPENAGC_PSBC_ROOT:-$repo_dir/../openagc-psbc}
output="$repo_dir/examples/bc_sampling/bc_probe_assets.h"
mode=${1:---check}

case "$mode" in
    --check|--write) ;;
    *) echo "usage: $0 [--check|--write]" >&2; exit 2 ;;
esac
if [ ! -f "$psbc_root/libopenagc_psbc.a" ]; then
    echo "missing host openagc-psbc archive: $psbc_root/libopenagc_psbc.a" >&2
    exit 2
fi

work=$(mktemp -d "${TMPDIR:-/tmp}/vulkan-ps5-bc-assets.XXXXXX")
trap 'rm -rf "$work"' EXIT
MACOSX_DEPLOYMENT_TARGET=26.0 clang -std=c11 -Wall -Wextra -Wpedantic \
    -I"$psbc_root/src" -I"$psbc_root/include/mesa" \
    "$script_dir/generate_bc_probe_assets.c" \
    "$psbc_root/libopenagc_psbc.a" -o "$work/generate"
"$work/generate" >"$work/bc_probe_assets.h"

if [ "$mode" = --write ]; then
    cp "$work/bc_probe_assets.h" "$output"
elif ! cmp -s "$work/bc_probe_assets.h" "$output"; then
    echo "BC probe assets are stale; run $0 --write" >&2
    diff -u "$output" "$work/bc_probe_assets.h" || true
    exit 1
fi
