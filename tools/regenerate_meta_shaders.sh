#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
glslang=${GLSLANG_VALIDATOR:-glslangValidator}
temporary=$(mktemp -d "${TMPDIR:-/tmp}/vulkan-ps5-meta.XXXXXX")
trap 'rm -rf "$temporary"' EXIT HUP INT TERM

"$glslang" -V --target-env vulkan1.1 \
    "$root/src/meta/clear_color.comp" \
    -o "$temporary/clear_color.spv"
cmake -DINPUT="$temporary/clear_color.spv" \
    -DOUTPUT="$temporary/clear_color_spv.h" \
    -DSYMBOL=vulkan_ps5_meta_clear_color_spv \
    -P "$root/cmake/embed_spirv.cmake"

if test "${1:-}" = --check; then
    cmp "$temporary/clear_color.spv" "$root/src/meta/clear_color.spv"
    cmp "$temporary/clear_color_spv.h" "$root/src/meta/clear_color_spv.h"
else
    cp "$temporary/clear_color.spv" "$root/src/meta/clear_color.spv"
    cp "$temporary/clear_color_spv.h" "$root/src/meta/clear_color_spv.h"
fi
