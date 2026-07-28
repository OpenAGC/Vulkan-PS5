# Vulkan-PS5

Vulkan-PS5 is an application-neutral Vulkan ICD for PlayStation 5 homebrew. The
current implementation includes the host-testable Milestone 1 ICD, the
Milestone 2 runtime-pipeline path, and the first Milestone 3 OpenAGC DCB
recording path. It exposes the
complete Vulkan 1.0/1.1 core entrypoint surface, conservative gfx1013 physical
device properties, two OpenAGC-backed PS5 GPU memory classes and
synchronization objects, loader dispatch, a static SDK library, and a
loader-compatible shared library. Features which do not yet have verified
OpenAGC hardware implementations are deliberately not advertised.

The ICD consumes OpenAGC's application-neutral gfx1013 capability query for
qualified dimensions, formats, sample counts, compute limits, and memory
profiles. `OPENAGC_ROOT`, `VULKAN_HEADERS_ROOT`, and `OPENAGC_PSBC_ROOT` remain
configurable, including `OPENAGC_PSBC_LIBRARY` for selecting the host or
Prospero archive. The installed SDK carries the matching compiler archive and
public header; consumers still resolve Vulkan-Headers and OpenAGC through the
exported CMake package dependencies.

## Build

```sh
cmake -S . -B build -DVULKAN_HEADERS_ROOT=../Vulkan-Headers
cmake --build build
ctest --test-dir build --output-on-failure
```

Prospero builds select `libopenagc_psbc.prospero.a` automatically when cross
compiling. Final PS5 links require `libunwind`, `libc++abi`, `libc++`, and
`libm`. The `ps5-payload-libcxx` and `ps5-payload-openlibm` recipes in
`/Users/bizkut/Downloads/PS5/homebrew/pacbrew-repo` install those target headers
and archives into the payload SDK. Merely having the recipe checkout is not
sufficient—the packages must be built and installed into the SDK prefix.

Applications link `VulkanPS5::ICD` after installing the package, or link
`libvulkan_ps5.a` directly. They use only standard Vulkan headers and APIs.

## Standalone compute and triangle samples

Configure with `-DVULKAN_PS5_BUILD_EXAMPLES=ON` to build
`vulkan_ps5_compute_example`. It uses only Vulkan 1.1 APIs, dispatches a runtime
compiled storage-buffer shader, waits on a fence, invalidates mapped memory,
and verifies 1,024 deterministic values. Command recording emits OpenAGC's
hardware-qualified gfx1013 compute-default groups once before the command
buffer's first dispatch. A Prospero cross-build produces
`vulkan_ps5_compute_example.elf`; FW 5.50 execution remains the qualification
gate. Running it on the generic host backend intentionally reports a mismatch
because that backend records submissions but does not execute shaders.

The same option builds `vulkan_ps5_triangle_example`. It renders a solid-green
triangle into a mapped 256x256 linear RGBA8 attachment through an ordinary
render pass, waits for completion, invalidates the allocation, and verifies the
center, background, and green-pixel count. Its Prospero output is
`vulkan_ps5_triangle_example.elf`; generic-host execution likewise reports the
expected all-zero readback.

`vulkan_ps5_indexed_textured_example` binds an interleaved position/UV vertex
buffer, a UINT16 index buffer containing `{1,2,3}` after a decoy vertex, and a
bilinear clamp sampler over a 2x2 RGBA8 image. Its readback oracle requires
triangle coverage, fully opaque sampled pixels, at least 16 distinct colors,
an interior center sample, and untouched background corners.

`vulkan_ps5_depth_example` combines a mapped linear RGBA8 target with an
OpenAGC-laid-out optimal D32 attachment. One near green triangle must occlude
an overlapping far red triangle while a separate far red triangle passes over
the initialized depth value. Its oracle checks exact interior colors, coverage,
and raw clear/near/far D32 words. Generic-host execution intentionally reports
unchanged memory because that backend does not execute GPU commands.

`vulkan_ps5_mrt_example` draws one triangle to two linear RGBA8 attachments.
Its fragment shader exports green to location 0 and magenta to location 1;
the mapped-memory oracle independently checks coverage, exact color, center,
and untouched background for both targets.

`vulkan_ps5_query_example` is a query-enabled build of the deterministic
triangle workload. It brackets the draw with an occlusion query and requires
the returned 64-bit sample count and availability bit to match the mapped
green-pixel oracle exactly. It uses `VK_EXT_host_query_reset` so query begin/end
hardware can be qualified independently from command-reset PM4. The generic
backend intentionally reports an unavailable zero result because it records
but does not execute the stream.

Query hardware qualification is deliberately staged. The
`vulkan_ps5_query_lifecycle_probe` creates and host-resets a pool but emits no
query PM4. The `vulkan_ps5_query_reset_probe` adds only the corrected command
reset `WRITE_DATA`. The `vulkan_ps5_query_idle_probe` adds occlusion begin/end
snapshots around an empty render pass and requires an available zero result.
The full target then counts a live draw. Run one stage explicitly; the full
stage is now part of the repeated Milestone 3 gate after passing twice on FW
5.50. The narrower stages remain useful for packet-level regression diagnosis:

```sh
PS5_HOST=10.0.1.41 examples/run_fw550_query_probes.sh lifecycle
PS5_HOST=10.0.1.41 examples/run_fw550_query_probes.sh reset
PS5_HOST=10.0.1.41 examples/run_fw550_query_probes.sh idle
PS5_HOST=10.0.1.41 examples/run_fw550_query_probes.sh full
```

When the console is online, deploy either Prospero ELF through the foreground
etaHEN websrv path so its stdout is returned to the terminal:

```sh
PS5_HOST=10.0.1.41 examples/deploy_websrv.sh \
  build-prospero-m2/vulkan_ps5_compute_example.elf vulkan_ps5_compute
PS5_HOST=10.0.1.41 examples/deploy_websrv.sh \
  build-prospero-m2/vulkan_ps5_triangle_example.elf vulkan_ps5_triangle
```

The Milestone 3 runner checks websrv reachability, performs
two foreground runs of each sample, requires the exact compute, triangle,
indexed-textured, depth, MRT, and query PASS oracles, and retains stdout under
`examples/qualification-logs/`:

```sh
PS5_HOST=10.0.1.41 examples/run_fw550_m3.sh
```

The 2026-07-27 UTC FW `0x05500008` qualification passed all six runs: both
compute runs verified 1,024 deterministic values, both triangle runs verified
exactly 18,432 green pixels, and both indexed-textured runs verified exactly
18,432 opaque sampled pixels with at least 64 distinct colors. See
`analysis/fw550_indexed_textured_qualification_20260727.md` for the retained
revision and artifact evidence.

The 2026-07-27 UTC `20260727T231245Z` run qualified depth and MRT twice, but its
first query submission hung the GPU. Packet-level recovery then qualified the
query lifecycle, corrected reset, idle begin/end, and live counting in stages.
The final `20260728T003630Z` gate passed all six workloads twice: query returned
18,432 samples matching 18,432 mapped pixels, depth produced identical
`43418/12288/9830` raw-word counts, and MRT produced 18,432 pixels in both
targets. The full investigation and retained-log map are recorded in
`analysis/fw550_depth_mrt_query_qualification_20260727.md`.

Set `VULKAN_PS5_PROSPERO_BUILD`, `VULKAN_PS5_FW550_RUNS`, or
`VULKAN_PS5_FW550_LOG_DIR` to override the build directory, repeat count, or
log destination.

The Khronos validation test is enabled automatically when the host Vulkan loader
and Validation Layers are installed. It exercises instance/device lifecycle,
property chains, memory, buffers, images, views, render-pass/framebuffer objects,
command buffers, submission, and fences while failing on any VVL warning/error.

Runtime shader compilation and host-verifiable OpenAGC DCB recording are now
integrated. Shader code and submission DCBs use OpenAGC GPU-visible flexible
memory, while `vkQueueSubmit` appends a monotonic EOP label, submits through
OpenAGC, and waits with a bounded timeout before host signaling. VideoOut WSI
and FW 5.50 qualification remain separate milestones. Graphics VS/PS and
compute CS pipeline creation
compile SPIR-V at runtime with complete vertex, descriptor/pipeline-layout,
push-constant, specialization-constant, entry-point, and render-pass color
context. Geometry and tessellation stage fusion are also wired through pipeline
creation, including independent specialization data for fused stages, but their
Vulkan feature bits remain disabled until the resulting pipelines are qualified
on hardware. Compute dispatch and a no-input triangle draw now emit real
gfx1013 `DISPATCH_DIRECT` and `DRAW_INDEX_AUTO` packet sequences. Compute
uniform/storage-buffer descriptor sets are stored through standard Vulkan
updates, encoded into GPU-visible OpenAGC tables, and patched into the compiler
selected user-SGPR immediately before dispatch. Indexed draws bind standard
Vulkan vertex/index buffers, build per-draw GPU-visible gfx1013 vertex tables,
and emit `DRAW_INDEX_2` for UINT16 or UINT32 indices. Combined and separate
sampled-image/sampler descriptors for linear RGBA8/BGRA8 images are encoded
through OpenAGC and bound to compiler-selected graphics SGPRs. Linear image
allocation and subresource layouts use the gfx1013-required 256-byte row pitch;
the sampled-image path is repeatedly hardware-qualified. Dynamic buffer
offsets and optional sparse, protected, external-handle, multiview, YCbCr, and
timeline features remain unavailable.

On Prospero, `vkCreateDevice` reaches OpenAGC initialization, which now keeps
the FW-specific GPU process-authorization preparation inside its `/dev/gc`
backend. Standalone Vulkan applications do not include or call the OpenAGC
hardware-test credential header.

The initial graphics render-pass path supports one single-sampled linear color
attachment or up to eight MRT color attachments, an optional OpenAGC-laid-out
optimal depth/stencil attachment, one
inline subpass, load/don't-care operations, fixed full-range viewport/scissor
state, fill rasterization without culling, disabled blending, and all-component
writes. MRT currently requires blending disabled and all components enabled on
every target. Static depth compare/write and front/back stencil state are translated
to typed OpenAGC draw state. Begin/end render pass translate layouts to OpenAGC
resource transitions, emit the qualified gfx1013 frame prologue, bind attachment
addresses, and restore host-readable cache state after drawing. Depth/stencil
clears remain unavailable; the first hardware gate initializes mapped direct
memory and uses `LOAD`.

Occlusion query pools use GPU-visible storage and OpenAGC-owned gfx1013 ZPASS
snapshots. Reset is command-ordered, end-query publishes a separate EOP
availability label, and `vkGetQueryPoolResults` supports 32/64-bit values,
availability, partial results, and bounded waits. Precise occlusion and
timestamps remain disabled pending hardware qualification.
`VK_EXT_host_query_reset` is advertised with `hostQueryReset = VK_TRUE` and
flushes the selected query slots after a host-side reset.
