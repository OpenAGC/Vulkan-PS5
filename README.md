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

When the console is online, deploy either Prospero ELF through the foreground
etaHEN websrv path so its stdout is returned to the terminal:

```sh
PS5_HOST=10.0.1.41 examples/deploy_websrv.sh \
  build-prospero-m2/vulkan_ps5_compute_example.elf vulkan_ps5_compute
PS5_HOST=10.0.1.41 examples/deploy_websrv.sh \
  build-prospero-m2/vulkan_ps5_triangle_example.elf vulkan_ps5_triangle
```

The Milestone 3 qualification runner checks websrv reachability, performs two
foreground runs of each sample, requires the exact compute and triangle PASS
oracles, and retains stdout under `examples/qualification-logs/`:

```sh
PS5_HOST=10.0.1.41 examples/run_fw550_m3.sh
```

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
selected user-SGPR immediately before dispatch. Image/sampler descriptors,
dynamic buffer offsets, vertex tables, hardware readback/display evidence,
and optional sparse, protected, external-handle, multiview, YCbCr, and timeline
features remain unavailable.

On Prospero, `vkCreateDevice` reaches OpenAGC initialization, which now keeps
the FW-specific GPU process-authorization preparation inside its `/dev/gc`
backend. Standalone Vulkan applications do not include or call the OpenAGC
hardware-test credential header.

The initial graphics render-pass path supports one single-sampled linear color
attachment, one inline subpass, load/don't-care operations, fixed full-range
viewport/scissor state, fill rasterization without culling, disabled blending,
and all-component writes. Begin/end render pass translate layouts to OpenAGC
resource transitions, emit the qualified gfx1013 frame prologue, bind the
attachment address, and restore host-readable cache state after drawing.
