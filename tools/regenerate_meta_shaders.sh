#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
glslang=${GLSLANG_VALIDATOR:-glslangValidator}
temporary=$(mktemp -d "${TMPDIR:-/tmp}/vulkan-ps5-meta.XXXXXX")
trap 'rm -rf "$temporary"' EXIT HUP INT TERM

"$glslang" -V --target-env vulkan1.1 \
    "$root/src/meta/clear_color.comp" \
    -o "$temporary/clear_color.spv"
for shader in clear_attachment.vert clear_attachment_color.frag \
    clear_attachment_depth.frag clear_attachment_stencil.frag blit.frag \
    resolve.frag; do
    "$glslang" -V --target-env vulkan1.1 \
        "$root/src/meta/$shader" -o "$temporary/$shader.spv"
done
cmake -DINPUT="$temporary/clear_color.spv" \
    -DOUTPUT="$temporary/clear_color_spv.h" \
    -DSYMBOL=vulkan_ps5_meta_clear_color_spv \
    -P "$root/cmake/embed_spirv.cmake"
"$glslang" -V --target-env vulkan1.1 \
    "$root/examples/resolve/source.vert" \
    -o "$temporary/resolve_source.vert.spv"
cmake -DINPUT="$temporary/resolve_source.vert.spv" \
    -DOUTPUT="$temporary/resolve_source_vert_spv.h" \
    -DSYMBOL=vulkan_ps5_resolve_source_vert_spv \
    -P "$root/cmake/embed_spirv.cmake"
"$glslang" -V --target-env vulkan1.1 \
    "$root/examples/resolve/source.frag" \
    -o "$temporary/resolve_source.frag.spv"
cmake -DINPUT="$temporary/resolve_source.frag.spv" \
    -DOUTPUT="$temporary/resolve_source_frag_spv.h" \
    -DSYMBOL=vulkan_ps5_resolve_source_frag_spv \
    -P "$root/cmake/embed_spirv.cmake"
for record in \
    "clear_attachment.vert:vulkan_ps5_meta_clear_attachment_vert_spv:clear_attachment_vert_spv.h" \
    "clear_attachment_color.frag:vulkan_ps5_meta_clear_attachment_color_frag_spv:clear_attachment_color_frag_spv.h" \
    "clear_attachment_depth.frag:vulkan_ps5_meta_clear_attachment_depth_frag_spv:clear_attachment_depth_frag_spv.h" \
    "clear_attachment_stencil.frag:vulkan_ps5_meta_clear_attachment_stencil_frag_spv:clear_attachment_stencil_frag_spv.h" \
    "blit.frag:vulkan_ps5_meta_blit_frag_spv:blit_frag_spv.h" \
    "resolve.frag:vulkan_ps5_meta_resolve_frag_spv:resolve_frag_spv.h"; do
    shader=${record%%:*}
    remainder=${record#*:}
    symbol=${remainder%%:*}
    header=${remainder#*:}
    cmake -DINPUT="$temporary/$shader.spv" \
        -DOUTPUT="$temporary/$header" -DSYMBOL="$symbol" \
        -P "$root/cmake/embed_spirv.cmake"
done

if test "${1:-}" = --check; then
    cmp "$temporary/clear_color.spv" "$root/src/meta/clear_color.spv"
    cmp "$temporary/clear_color_spv.h" "$root/src/meta/clear_color_spv.h"
    cmp "$temporary/resolve_source.frag.spv" \
        "$root/examples/resolve/source.frag.spv"
    cmp "$temporary/resolve_source.vert.spv" \
        "$root/examples/resolve/source.vert.spv"
    cmp "$temporary/resolve_source_vert_spv.h" \
        "$root/examples/resolve/source_vert_spv.h"
    cmp "$temporary/resolve_source_frag_spv.h" \
        "$root/examples/resolve/source_frag_spv.h"
    for shader in clear_attachment.vert clear_attachment_color.frag \
        clear_attachment_depth.frag clear_attachment_stencil.frag blit.frag \
        resolve.frag; do
        cmp "$temporary/$shader.spv" "$root/src/meta/$shader.spv"
    done
    for header in clear_attachment_vert_spv.h \
        clear_attachment_color_frag_spv.h clear_attachment_depth_frag_spv.h \
        clear_attachment_stencil_frag_spv.h blit_frag_spv.h \
        resolve_frag_spv.h; do
        cmp "$temporary/$header" "$root/src/meta/$header"
    done
else
    cp "$temporary/clear_color.spv" "$root/src/meta/clear_color.spv"
    cp "$temporary/clear_color_spv.h" "$root/src/meta/clear_color_spv.h"
    cp "$temporary/resolve_source.frag.spv" \
        "$root/examples/resolve/source.frag.spv"
    cp "$temporary/resolve_source.vert.spv" \
        "$root/examples/resolve/source.vert.spv"
    cp "$temporary/resolve_source_vert_spv.h" \
        "$root/examples/resolve/source_vert_spv.h"
    cp "$temporary/resolve_source_frag_spv.h" \
        "$root/examples/resolve/source_frag_spv.h"
    for shader in clear_attachment.vert clear_attachment_color.frag \
        clear_attachment_depth.frag clear_attachment_stencil.frag blit.frag \
        resolve.frag; do
        cp "$temporary/$shader.spv" "$root/src/meta/$shader.spv"
    done
    for header in clear_attachment_vert_spv.h \
        clear_attachment_color_frag_spv.h clear_attachment_depth_frag_spv.h \
        clear_attachment_stencil_frag_spv.h blit_frag_spv.h \
        resolve_frag_spv.h; do
        cp "$temporary/$header" "$root/src/meta/$header"
    done
fi
