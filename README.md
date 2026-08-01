# Vulkan-PS5

Vulkan-PS5 is an application-neutral Vulkan ICD for PlayStation 5 homebrew. The
current implementation translates through OpenAGC's public native device,
resource, shader, pipeline, command, synchronization, and presentation APIs
plus `openagc-psbc` compiler metadata. It includes the host-testable Milestone 1
ICD, the Milestone 2 runtime-pipeline path, the hardware-qualified Milestone 3
OpenAGC DCB path, and the hardware-qualified Milestone 4 headless-surface/
swapchain path. Milestone 5 also qualifies the relocatable SDK package through
a separate standard-Vulkan consumer on both host and FW 5.50.

## Architecture Direction

Vulkan-PS5 is a constrained translation layer above OpenAGC's native runtime.
The former direct integration remains historical hardware evidence, while the
zero-direct-call candidate must earn its own FW 5.50 qualification.

The target ownership boundary is:

- OpenAGC owns exact firmware-profile selection, device/queue lifecycle,
  buffer and image allocation, shader objects, validated graphics/compute
  pipelines, command storage, resource states and cache transitions, fences,
  submission, deferred retirement, diagnostics, capture records, and VideoOut.
- `openagc-psbc` owns SPIR-V compilation and versioned reflection for
  descriptors, push constants, vertex inputs, user SGPRs, color exports, wave
  mode, tessellation, geometry/NGG, and stage linkage.
- Vulkan-PS5 owns Vulkan object semantics, feature/extension negotiation,
  `pNext` handling, descriptor/update rules, render-pass or dynamic-rendering
  translation, Vulkan memory requirements, synchronization semantics, WSI, and
  conservative capability advertisement.

Vulkan-PS5 must not retain a second PM4 emitter, firmware table, GPU heap
allocator, shader-reflection ABI, resource-state/cache model, queue/fence
backend, or VideoOut patch policy after the corresponding native OpenAGC slice
is available. Temporary direct paths remain permitted only while migrating a
feature and must stay covered by the existing host and hardware regressions.

Migration proceeds by complete vertical slices rather than a flag-day rewrite:

1. Freeze the current ICD feature/format/extension matrix and deterministic
   host/FW 5.50 evidence as the comparison baseline.
2. Map instance/device/queue capability discovery to `AgcDevice`, `AgcQueue`,
   and `agcGetRuntimeInfo` without exposing firmware branches to applications.
3. Move buffers, images, views, samplers, shaders, pipelines, command buffers,
   transitions, submission, and synchronization to native objects in that
   dependency order.
4. Move WSI onto OpenAGC-owned scanout resources and presentation lifecycle.
5. Re-run loader/VVL, sanitizer, package-relocation, deterministic readback,
   and bounded hardware gates after every migrated slice.
6. Qualify selected standard Vulkan applications through one firmware-neutral
   build on both FW 5.50 and FW 11.60. Existing FW 5.50 passes do not by
   themselves qualify the native-runtime migration or FW 11.60.

The direct-call migration is host-complete. The checked inventory in
`analysis/native_runtime_calls.tsv` has zero rows, and its CTest gate rejects
any reintroduced low-level call. Every `VkDevice` owns an OpenAGC `AgcDevice` plus native graphics
and compute queues. OpenAGC API 26 explicitly supports concurrent logical
devices over the selected process backend, and the Vulkan lifecycle regression
creates two devices concurrently. OpenAGC API 28 now backs `VkDeviceMemory`
and bound `VkBuffer` objects with explicit `AgcMemory` and placed `AgcBuffer`
handles, uses native image layouts and placed `AgcImage`, and gives compatible
views and all samplers native `AgcImageView`/`AgcSampler` backing.
OpenAGC API 33/PSBC API 18 additionally give every compiled stage a native
shader, every compute pipeline a native pipeline, and compatible point, line,
triangle list/strip/fan, geometry—including adjacency—and tessellation
graphics pipelines native ownership,
including polygon modes, culling, rasterizer discard, strip/fan primitive
restart, depth clamp/bias, logic operations, static/dynamic line width, and
pipeline switching. Shader allocation, relocation, cache flush, fusion, and
lifetime now belong exclusively to `AgcShader`. Vulkan command buffers also
own paired queue-typed native command streams, with the graphics DCB serving as the ordered
graphics-plus-compute stream. Pipeline binds, supported typed buffer/image
barriers, and explicitly transitioned buffer copies are mirrored through
native APIs. Compute dispatch plus direct, indexed, geometry, and tessellation
draws now require the typed native path; pipeline changes rebind native
descriptor and vertex state. Descriptor encoding, tessellation resources,
image layout, command storage, submission, and finite waits are OpenAGC-owned;
the legacy encoder and submission fallback have been deleted.

The current compressed-texture baseline exposes all 14 Vulkan BC1-BC7
UNORM/SNORM/SRGB/UFLOAT/SFLOAT formats for sampled, linearly filtered, and
transfer image use. Images may contain complete mip chains, including cube and
cube-array layers, and views may select nonzero mip intervals through native
OpenAGC descriptors. D24, ASTC, and ETC remain deliberately unsupported rather
than being aliased to incompatible hardware formats. This is a general Vulkan
contract used by Eden and other applications; `analysis/eden-compatibility.md`
tracks Eden only as one demanding consumer.

`vulkan_ps5_bc_sampling_probe` executes a compute shader that samples one
committed deterministic 4x4 block in each advertised BC format through Vulkan
sampler and combined-image-sampler descriptors. Regenerate or verify the
Mesa-codec-derived assets with `tools/regenerate_bc_probe_assets.sh --write`
or `tools/regenerate_bc_probe_assets.sh --check`. The guarded FW 5.50 runner is
`examples/run_fw550_bc_sampling.sh`; its twice-passed ELF is pinned at SHA-256
`601d0d2694c819e48140b429bb9e16b473ea91b5c9ad9eaac69bb8ae8624b639`.
This qualifies direct-upload linear-image sampling with nearest filtering;
filtered/cube sampling and the final FW 11.60 replay remain pending.

`vulkan_ps5_bc_copy_probe` separately copies exact compressed bytes for every
advertised BC format from source mip 0/1 into destination mip 1/2, verifies all
28 regions, and checks that destination mip 0 remains untouched. Run its
cleanup-first FW 5.50 gate with `examples/run_fw550_bc_copy.sh`. The twice-
passed ELF is pinned at SHA-256
`0c97df8a72c21577c543dd64649d3f3fc5e0e7f74190adf4ab2ba235bd3b74d4`.

`vkCmdClearColorImage` has an application-neutral, range-aware implementation
for every advertised uncompressed color format. A committed, reproducible meta
compute shader writes packed patterns through a placed storage-buffer alias;
regular array layers are batched into one two-dimensional dispatch per mip.
Exact OpenAGC subresource layouts bound each mip/layer write, and the Vulkan
compute or graphics state is restored for subsequent commands. The path handles arbitrary
float/UNORM values, half/packed-float conversion, SRGB encoding, and GENERAL
or TRANSFER_DST layouts using only public OpenAGC APIs. Regenerate the embedded
SPIR-V with `tools/regenerate_meta_shaders.sh`; `--check` verifies committed
bytes. Compressed and depth/stencil formats are rejected by this color path.

The Eden-required scalar/vector normalized and integer images expose sampled,
storage, color-attachment, and transfer use with filtering and blit restricted
to normalized classes. They are included in the expanded bounded FW 5.50
gate, which creates 38 native views, clears and reads back 2,432 exact
component patterns, tears down, and immediately relaunches the identical ELF.
Run it with
`PS5_HOST=10.0.1.41 VULKAN_PS5_LIVE_KLOG=1
examples/run_fw550_integer_formats.sh`. A separate formatless-storage gate
writes all 30 storage-capable audited formats through float, unsigned, and
signed compute shaders and verifies 480 exact pixels. A nearest-sampling gate
then samples all 38 formats through numeric-class-matched compute shaders and
passes 152 exact result components twice from one pinned ELF. The attachment
gate renders all 36 color-attachment formats through float, unsigned, and
signed fragment exports and reads back 36 bit-exact pixels twice on FW
5.500.008. See `analysis/fw550_format_sampling_20260802.md` and
`analysis/fw550_format_attachments_20260802.md`.

OpenAGC API 48 adds the related R/RG 16-bit normalized and integer, RGBA16
normalized, and R/RG 32-bit integer image forms. Their Vulkan queries preserve
the numeric-class distinction—normalized images may filter and blit while
integer images may not—and do not advertise unimplemented texel buffers. The
host and Prospero gates cover exact native descriptors, scalar/vector render
exports, image/view creation, and clear packing. The expanded bounded FW 5.50
probe passes all eighteen new formats and 1,152 exact clear/readback pixels
twice with immediate relaunch. Storage-image execution is now qualified as
part of the 30-format gate, sampled-image execution is qualified by the
38-format gate, and the 36-format attachment-export gate is qualified on FW
5.500.008.

OpenAGC API 49 adds exact native R8/RG8 SNORM, UINT, and SINT layouts,
descriptors, color targets, and pipeline compatibility. Vulkan advertises
their numeric-class-appropriate sampled, storage, attachment, transfer,
filter, and blit features, and also corrects the pre-existing R8/RG8 UNORM
storage-image creation gap. The pinned FW 5.50 probe passed all eight R8/RG8
forms twice with exact bits as part of its 26-format oracle. See
`analysis/fw550_r8_rg8_formats_20260801.md`.

OpenAGC API 50 adds exact RGBA8 SNORM/UINT/SINT, RGB10A2 UINT, and
BGR10A2 UNORM descriptors and render-target classes. Vulkan exposes them as
the corresponding packed `A8B8G8R8`, `A2B10G10R10`, and `A2R10G10B10`
formats, preserving normalized/integer filtering, blit, and storage
distinctions. The pinned FW 5.50 probe passed all 31 formats and 1,984 exact
pixels twice. See `analysis/fw550_packed_formats_20260801.md`.

OpenAGC API 51 adds exact R5G6B5, B5G6R5, R5G5B5A1, A1R5G5B5,
A4B4G4R4, and R4G4 UNORM packed images. Vulkan exposes the five renderable
16-bit forms without false storage/texel-buffer claims and keeps R4G4
sampled-only. The pinned FW 5.50 probe passed all 37 formats and 2,368 exact
pixels twice. See `analysis/fw550_packed16_formats_20260801.md`.

OpenAGC API 52 adds RGB9E5 shared-exponent sampled images. Vulkan exposes
`VK_FORMAT_E5B9G9R9_UFLOAT_PACK32` as sampled/filterable, transferable, and a
blit source without falsely advertising color-attachment or storage support.
The pinned FW 5.50 probe passed all 38 formats and 2,432 exact pixels twice.
`VK_FORMAT_R32G32B32_SFLOAT` remains intentionally image-unsupported because
gfx10.3 defines that encoding as buffer-only. See
`analysis/fw550_rgb9e5_format_20260801.md`.

The scalar/vector storage-image gate uses formatless float, unsigned, and
signed compute shaders, one immutable descriptor set per target image, and
reflected push constants. It writes every one of the 30 storage-capable
formats above and passed twice on FW 5.500.008 with 480 bit-exact pixels,
bounded synchronization, teardown, and immediate relaunch. The first run also
exposed and corrected the clear packer's Vulkan-invalid SNORM endpoint: -1.0
now converts to -127/-32767 instead of the reserved -128/-32768 encodings.
See `analysis/fw550_format_storage_20260802.md`.

`vkCmdClearAttachments` and render-pass/dynamic-rendering `loadOp=CLEAR`
share an application-neutral graphics-meta path. It supports arbitrary
validated rectangles for every advertised color attachment format and the
advertised single-sample D16, D32, S8, D16+S8, and D32+S8 depth/stencil
formats, including separate or combined depth/stencil aspects and depth-only
dynamic rendering. Pipelines are cached lazily by format/aspect, use only
public OpenAGC objects and commands, and restore application state before later
draws. The identical pinned ELF twice produced the exact 64x64
dynamic-rendering oracle `green=1152 clear=2944` on both FW 5.500.008 and FW
11.600.005, with clean teardown and immediate relaunch. Depth/stencil pixels
remain a separate qualification gate.

`PLAN.md` is authoritative for this migration. `STATUS.md` records what the
current ICD has actually implemented and qualified; a planned native mapping
must not be advertised until its host and exact-firmware gates pass.

The SDL accelerated-OpenGL track is now measured by the pinned Mesa/Zink GL
2.1 profile in `analysis/zink-compatibility.md`. This slice adds render-pass 2,
descriptor update templates, timeline semaphores, mutable/incremental WSI, and
rectangular line-rasterization contracts, dynamic rendering, custom border
colors with image-view swizzle, maintenance5, and Vulkan 1.2. Scalar block
layout is compiled by a real storage-buffer pipeline, and `alphaToOne` is baked
into the gfx1013 pixel epilog. `VK_EXT_depth_clip_enable` is also enumerated,
feature-enabled, and translated to OpenAGC's explicit depth-clip state; its
depth-sensitive probe passes twice with identical bytes and one gfx1013 clip
sequence on both FW 5.50 and FW 11.60. The final SDL/EGL/Zink libraries also
pass three consecutive FW 11.60 runs and two FW 5.50 replays with exact
readback, presentation, teardown, and immediate relaunch.
The live strict report is now
`api=0 extensions=0 features=0 total=0`, with FW 5.500.008 readback evidence
for scalar layout, alpha-to-one, dynamic rendering, and swizzled custom border
sampling. SDL retains OSMesa as its conservative fallback; the pinned Mesa
runtime now also passes through the PS5 EGL/WSI bridge.

The pinned integration now passes end to end on FW 5.500.008. Vulkan-PS5
records reflected push constants, dynamic-rendering RGBA8/BGRA8 clears, global
color-to-transfer dependencies, Mesa's mutable RGBA/A8B8G8R8 readback format
class, and Zink's compatible null-array `vkCmdBindVertexBuffers2` form. The
aliased clear buffer performs an explicit copy-write release before image use,
and Vulkan object destruction is deferred until OpenAGC command recycling
releases native references. Two immediate guarded runs returned exact RGBA
`64,128,191,255`, presented, completed native teardown, self-exited, and
relaunched without reboot. The identical final libraries subsequently passed
three FW 11.60 runs and two FW 5.50 replays; the host suite remains 48/48.

The general clear path now also implements `vkCmdClearDepthStencilImage` for
D16, D32, S8, D16+S8, and D32+S8 transfer-destination images. Depth and stencil
planes use their independently queried OpenAGC subresource layouts, arbitrary
selected mip/layer ranges are supported, and regular array layers batch into
one compute dispatch per selected plane and mip. One pinned FW 5.500.008 ELF
passed twice with exact readback for all five formats (`depth=256
stencil=192`), bounded synchronization, teardown, and immediate relaunch.
D24 remains unadvertised, 4x depth/stencil clears remain fail-closed, and the
identical FW 11.60 replay remains pending. Partial render-area color attachment
clears are separately qualified on both firmware endpoints; depth/stencil
attachment pixels remain pending.

`vkCmdBlitImage` now records application-neutral 2D color blits through a
reproducible graphics-meta shader and public OpenAGC objects. It supports
nearest and linear filtering, scaled and axis-reversed regions, mip/layer
selection, BC or uncompressed sampled sources, and uncompressed color
destinations. The same path now covers 3D-to-3D and mixed 2D/3D blits by
sampling a typed 3D view and drawing each selected destination depth slice.
Self-image 2D blits are supported when the complete source and destination
subresource sets are disjoint; feedback through one mip/layer remains
fail-closed. Format queries advertise `BLIT_SRC` for BC formats and both blit
directions for the supported uncompressed color matrix. Depth/stencil and
compressed destinations remain fail-closed. The original 2D pinned
Prospero ELF SHA-256 is
`a2ad727201bea7ad40d1fa85e5bda566d27255a6999d1cf96006e0fcdeecd82d`;
two cleanup-first FW 5.500.008 runs each read back exactly 256 nearest-scaled
pixels and 144 untouched guards with immediate relaunch. The identical ELF
then passed the same oracle twice on FW 11.600.005; its FTP round-trip SHA-256
remained identical.

The 3D/mixed/self extension is verified by all 48 normal and sanitizer suites,
has byte-reproducible shader artifacts, and builds cleanly as static and shared
Prospero libraries. The pinned FW 5.50 ELF SHA-256 is
`de13de9e50c8dc9a0c223f7dd56ea2f0bb8d8b2fb475029d38dae75953c67c47`;
two cleanup-first runs each matched all 64 volume and 16 self-image pixels,
self-exited, and left no process behind. FW 11.60 replay stays deferred to the
final pinned release candidate.

`vkCmdResolveImage` now implements application-neutral 4x-to-1x 2D color
resolves through a reproducible `sampler2DMS` graphics-meta shader. It validates
Vulkan usage, layouts, formats, aspects, mip/layer ranges, offsets, extents,
and bounds; unsupported depth/stencil, 3D, self, compressed, and non-4x forms
fail closed. The pinned Prospero ELF SHA-256 is
`acd7aaf9b536f9335d1d69609eaa5a80d366ad040df6e7ce48fe8f6ddfb211de`.
Two cleanup-first runs on each of FW 5.500.008 and FW 11.600.005 read back all
1,024 pixels as the exact four-sample average `0xff00ff80`, self-exited, and
allowed immediate relaunch. Development qualification now uses FW 5.50 per
slice; FW 11.60 is reserved for the final pinned release-candidate replay.

Graphics pipeline creation accepts the Vulkan depth-only form: dynamic
rendering may declare zero color formats, `VkPipelineColorBlendStateCreateInfo`
may contain zero attachments, and a depth/stencil pipeline is still translated
to a native OpenAGC graphics pipeline. This removes a general Vulkan contract
gap needed by shadow/depth passes and by the attachment-clear meta pipeline.

Milestone 6 is tracked by `analysis/eden-compatibility.md` and the
`vulkan_ps5.eden_profile_report` test. The initial Eden suitability baseline
had 30 hard gaps; the live ICD profile now reports
`extensions=0 features=0 limits=0 queues=0 total=0` without an
application-specific bypass. The final `multiViewport` closure exposes 16
slots and supports static and dynamic viewport/scissor arrays through typed
OpenAGC state. Two public FW 5.50 gates each produced exactly 9,216 green
pixels in viewport zero and 9,216 red pixels in viewport one, then self-exited
with the console responsive. The public-path Prospero ELF SHA-256 is
`72adfa0db768fc9ccf113b1c46eecc9b6467edba9d8a9ea62a554acf1a5351d7`.
Eden's separate Prospero surface/build/static-entrypoint integration remains
application work rather than an ICD capability gap.
`VK_KHR_sampler_mirror_clamp_to_edge` is enumerated and accepted after both its
internal-path and extension-enabled FW 5.50 probes produced 18,432 gray pixels
with exact center `0xff808080`, clean process exit, and clean target-only klog.
The public-path Prospero ELF SHA-256 is
`6b591dfe79c69f32cc9efdb641ab686183b0c7c0e032df7f3892f6e3357ce78f`.
The runtime compiler and graphics-pipeline path also accept instance-rate
vertex bindings with nonzero `VK_EXT_vertex_attribute_divisor` divisors. Both
the internal-path and extension/feature-enabled FW 5.50 gates produced an
exact-white 18,432-pixel triangle with center `0xffffffff` for divisor two over
four instances, followed by clean process exit and clean target-only klog. The
extension is enumerated; legacy EXT and promoted feature queries expose
nonzero divisor support while zero divisor and nonzero-first-instance support
remain false. The public-path ELF SHA-256 is
`5647b97d9ad8944028c4e242c49503a36f307ecc9ff603d765aec2f56b0c1503`.
Sampler creation also carries validated 1x-16x anisotropy into gfx1013's
anisotropic point/linear modes and maximum-ratio field. `samplerAnisotropy` is
advertised through legacy and Features2 queries and accepted through both
device-creation paths. Internal and public-path FW 5.50 gates each rendered
equal 9,830-pixel bilinear and 16x-anisotropic footprints; their neutral-gray
mean absolute deviations were 63 and 5 respectively, followed by clean process
exit and clean target-only klog. The public-path ELF SHA-256 is
`28319bef31f227ea45b9aacc35138e10b9cb136b88dbba350bc2a562a16c49b9`.
Qualification runners select only `category=native_game` EXEC records and
require cleanup targets to retain the exact `eboot.bin` identity, preventing a
reused PID from targeting ShellUI or another system process.
Single and multi `vkCmdDrawIndirect`/`vkCmdDrawIndexedIndirect` recording now
uses OpenAGC's application-neutral gfx1013 indirect-draw wrapper instead of a
no-op. The ICD validates usage, alignment, stride, range, and index bindings,
and supplies compiler-selected base-vertex, start-instance, and DrawIndex
user-SGPR locations. Shaders using DrawIndex expand a Vulkan multi draw into
hardware-qualified single-indirect packets, programming DrawIndex for each
command; shaders that do not use it retain OpenAGC's FW 5.50-qualified native
multi packet. A 2026-07-28 attempt to use a speculative Mesa-style 10-dword
multi packet caused a PID-scoped GPU fault/reset, so that form was removed.
The standalone `vulkan_ps5_indirect_parameters_probe` narrows subsequent
qualification to one BaseVertex/BaseInstance indirect draw without DrawID or
multi-draw expansion. That diagnostic exposed the remaining fault: the ICD
left the non-indexed draw initiator at the indexed/DMA value zero. It now emits
gfx1013 `DI_SRC_SEL_AUTO_INDEX` value two for non-indexed indirect draws and
zero for indexed indirect draws, with both exact packet tails host-tested. The
corrected FW 5.50 run completed the GPU work, produced the exact green readback,
and did not reset the GPU. Its later SIGSEGV occurred only after PASS while
returning through the raw-ELF exit path. Prospero samples now self-terminate
through SystemService after Vulkan cleanup, and bounded runners strip embedded
NUL bytes from klog captures before PID and lifecycle checks. Runner tests cover
that binary-log case explicitly. The rebuilt FW 5.500.008 candidate then passed
the same exact one-draw oracle as PID 86, self-killed with matching app IDs,
left idle graphics queues, and produced neither a fatal signal nor a GPU reset.
Only the isolated raw-ELF `amount=0x4000` VM warning remained. The complete
two-draw diagnostic then showed that BaseVertex and DrawID worked while
BaseInstance did not. Mesa RADV's gfx10 vertex-user-data order is BaseVertex,
DrawID, then BaseInstance, but openagc-psbc metadata had exported BaseVertex,
BaseInstance, then DrawID. Compiler commit `d209d94` fixes that ordering and
locks it with a library regression. The rebuilt indexed and non-indexed
DrawID pipelines expand into single packets and pass packet-boundary-safe host
checks for stable register locations, the exact DrawIndex `0,0,1,0,1`
sequence, and the correct initiators. Both normal and ASAN/UBSAN suites pass
25/25, and the bounded runner requires the shared matching-self-kill lifecycle.

`vkCmdCopyBuffer` records application-neutral OpenAGC gfx1013 `DMA_DATA`
packets for every Vulkan copy region. Recording validates bound memory,
transfer usage, four-byte alignment, buffer and 48-bit address bounds,
source/destination aliasing, and total DCB capacity before emitting any packet.
The standalone `vulkan_ps5_buffer_copy_example` verifies two offset regions
and every untouched guard byte through deterministic mapped-memory readback.
The corrected bounded probe validates `firstVertex = 1`, `firstInstance = 1,2`,
and `gl_DrawID = 0,1` through an exact all-green readback split equally across
the left and right target halves. Both the internal and public FW 5.50 gates
reported `green=11472 left=5736 right=5736`, exited cleanly, and produced no
PID-scoped fatal signal or GPU reset. `multiDrawIndirect`,
`drawIndirectFirstInstance`, and `shaderDrawParameters` are now advertised and
accepted through their standard core, Features2, Vulkan 1.1, and standalone
feature paths. The public Prospero ELF has SHA-256
`d3d80356967a93a9a8cfa0000eaa09089ddd39872f897e73952fe788ab48aa29`.
Milestone 6 also includes a test-only, configurable VulkanMemoryAllocator
consumer matching Eden's dynamic-dispatch, externally synchronized upload,
download, stream, device-local, image, manual-bind, and suballocation patterns;
it passes both direct and loader/VVL host modes without adding VMA to the SDK's
public dependencies. The Prospero PIE passed its bounded FW 5.50 gate with
deterministic zero-allocation teardown and application-level SystemService
termination, so the allocator patterns are hardware-qualified at this scope.
It exposes the
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

### Targeted Vulkan CTS

The current host discovery baseline uses official Vulkan CTS tag
`vulkan-cts-1.4.6.1` at commit
`5c8aae22885448d70a2873e94a93b24b49505c32`. Load the host ICD directly so
CTS negotiates Vulkan-PS5's advertised API version instead of the macOS
loader's version:

```sh
deqp-vk \
  --deqp-vk-library-path=/absolute/path/to/libvulkan_ps5.dylib \
  '--deqp-case=dEQP-VK.info.*'
```

The first direct run exposed and now regression-tests
`vkGetDeviceProcAddr(device, "vkGetDeviceProcAddr")` self-lookup. The current
information group reaches completion: 18 pass, two correctly report
`NotSupported`, and one fails on mandatory Vulkan 1.2 features. The
sample-count failure is closed by capability-derived 4x color/depth/stencil
limits plus sampled RGBA8, D16, D32, and S8 image support. The sampled 4x depth
descriptor path is host-tested through public OpenAGC APIs; hardware pixel
execution is still pending. This is targeted discovery evidence, not a
conformance claim; `conformanceVersion` remains `0.0.0.0`.

Core `imagelessFramebuffer` is also implemented rather than query-only:
`vkCreateFramebuffer` validates and retains
`VkFramebufferAttachmentsCreateInfo`, and render-pass begin consumes
`VkRenderPassAttachmentBeginInfo` to validate and bind the selected views.
The official mandatory-feature probe confirms that both duplicated imageless
requirements are closed. Standard uniform-buffer layout is also compiler-
backed: the pipeline test reflects a `std430` UBO through `openagc-psbc` and
the native descriptor/pipeline path. Separate depth/stencil layouts now retain
RenderPass2 stencil metadata, accept distinct dynamic-rendering layouts,
validate writes per aspect, and conservatively map packed depth/stencil cache
state through public OpenAGC transitions. The normal and sanitizer suites pass
59/59 and the Prospero build is clean. The official mandatory-feature probe no
longer reports that feature; border-color swizzle, multiview, dynamic subgroup
broadcast ID, and subgroup extended types remain before the information group
can pass.

An in-tree `add_subdirectory` consumer may already provide its own
`Vulkan::Headers` alias. Vulkan-PS5 carries the selected header checkout in its
build/install include interface and does not export that helper target as a
link dependency, so embedding the ICD cannot leak a parent-owned
`Vulkan-Headers` target into `VulkanPS5Targets`.

## Reusable installed SDK

The Milestone 5 package test installs Vulkan-Headers, OpenAGC, the runtime
shader compiler, and Vulkan-PS5 into a fresh prefix, moves that prefix, and
then configures a separately copied consumer with only:

```cmake
find_package(VulkanPS5 CONFIG REQUIRED)
target_link_libraries(application PRIVATE VulkanPS5::ICD)
```

The consumer includes `<vulkan/vulkan.h>` and exercises an ordinary Vulkan
1.1 instance/device lifecycle. The relocation test rejects CMake metadata or
consumer link commands containing a source-workspace path and verifies that
the relocated `libvulkan_ps5.a`, `libopenagc.a`, and
`libopenagc_psbc.a` archives are used. Its Prospero mode also verifies the
transitive `kernel`, `SceVideoOut`, `unwind`, `c++abi`, `c++`, and `m` links
and rejects any installed `SceAgcDriver` dependency. The sample links
`SceSystemService` itself solely for safe raw ELF termination.

Run the host relocation check through CTest, or run the Prospero check
directly:

```sh
ctest --test-dir build -R vulkan_ps5.package_relocation --output-on-failure
sh tests/package_relocation.sh . build-prospero-m2 ../Vulkan-Headers \
  /path/to/ps5-payload-sdk/toolchain/prospero.cmake \
  build-prospero-m2/vulkan_ps5_package_consumer.elf
```

The Prospero toolchain searches packages only below its find roots, so the
test registers the relocated SDK as an additional `CMAKE_FIND_ROOT_PATH`.
Applications using an SDK installed directly in the payload sysroot need no
such override.

After a fresh `ps5 up` signal, the retained consumer has a dedicated bounded
hardware gate:

```sh
PS5_HOST=10.0.1.41 sh examples/run_fw550_package_consumer.sh
```

The runner performs exactly one deployment, requires
`package-consumer: PASS result=0`, verifies self-requested app termination and
exact-PID removal through ps5debug-NG, checks that websrv remains responsive,
and rejects fatal or unexpected PID-scoped kernel messages. It permits at most
the single `amount:0x4000` warning already proven to be the FW 5.50 raw-ELF
baseline.

The current consumer additionally creates two devices and queues concurrently,
exercises a timeline semaphore, destroys one device, then allocates and frees
memory through the surviving device. Its SHA-256 is
`1fd79429140e26884cc492761d8551cf7a2b8769c39066463d9f05f6c9fc5547`.
That exact candidate passed once on FW 5.500.008: PID 154 printed the PASS
oracle, self-terminated, was absent from ps5debug-NG, and left only the accepted
raw-ELF baseline warning. Evidence is retained in
`examples/qualification-logs/20260731T065637Z-package-consumer.log` and
`20260731T065637Z-package-consumer-target.klog`.

## Headless surface and swapchain sample

`VK_EXT_headless_surface` is the standard PS5 VideoOut surface. The ICD
advertises `VK_KHR_surface`, `VK_EXT_headless_surface`, and `VK_KHR_swapchain`,
reports a fixed 1920x1080 BGRA8-sRGB FIFO contract, and owns three linear
write-combined scanout images. Firmware patching, buffer registration, flip
events, bounded waits, and teardown remain inside OpenAGC.

`vulkan_ps5_swapchain_example` uses only standard Vulkan calls and runs 1,800
acquire/submit/present frames with binary semaphores and a fence. Its host run,
the nine-test ICD suite, runner safety simulation, and Validation Layers pass;
its Prospero ELF links with `-lSceSystemService -lunwind -lc++abi -lc++ -lm`.
The current candidate SHA-256 is
`0b1d87d02a5fbe480cc74890c613752bb55c2e7b5f4e729413314785e5302888`.
Run exactly one bounded FW 5.50 gate after an explicit console-availability
signal:

Finite image-acquisition timeouts use a condition-variable deadline. The WSI regression
also exhausts all three images, releases one from a delayed presentation
thread, and verifies that the waiting acquire wakes without holding the
swapchain lock across the VSYNC wait.

```sh
PS5_HOST=10.0.1.41 examples/run_fw550_swapchain.sh
```

The same gate can qualify an external Vulkan application without weakening
its cleanup or log checks. Override the ELF, remote directory, display label,
anchored PASS expression, and human-readable workload description; continue
to pin the application bytes with `VULKAN_PS5_SWAPCHAIN_EXPECTED_SHA256`:

```sh
PS5_HOST=10.0.1.41 \
VULKAN_PS5_QUALIFICATION_ELF=../eden-ps5/build-prospero/src/ps5/eden-ps5-vulkan-bootstrap.elf \
VULKAN_PS5_QUALIFICATION_REMOTE_NAME=eden_ps5_vulkan_bootstrap \
VULKAN_PS5_QUALIFICATION_LABEL=eden \
VULKAN_PS5_QUALIFICATION_PASS_PATTERN='^eden-ps5-bootstrap: PASS 600 frames$' \
VULKAN_PS5_QUALIFICATION_PASS_DESCRIPTION='600 frames and compute oracle' \
VULKAN_PS5_SWAPCHAIN_EXPECTED_SHA256=<pinned-sha256> \
examples/run_fw550_swapchain.sh
```

Remote names and labels are restricted to alphanumerics, `_`, and `-`; empty
or oversized PASS controls fail before cleanup or upload. The runner safety
test covers both the default swapchain artifact and this external-app form.

An application with a bounded external launch or mode contract can use this
same gate without weakening the ELF pin. Set
`VULKAN_PS5_QUALIFICATION_SIDECAR`, its leaf-only
`VULKAN_PS5_QUALIFICATION_SIDECAR_REMOTE_NAME`, and
`VULKAN_PS5_QUALIFICATION_SIDECAR_EXPECTED_SHA256`. The runner verifies the
sidecar locally and after FTP upload before launching the ELF. A missing file,
unsafe remote name, wrong hash, or only a partial sidecar tuple fails before
console mutation.

For a workload that also needs one external data artifact, the equivalent
`VULKAN_PS5_QUALIFICATION_ASSET`,
`VULKAN_PS5_QUALIFICATION_ASSET_REMOTE_NAME`, and
`VULKAN_PS5_QUALIFICATION_ASSET_EXPECTED_SHA256` tuple receives the same local
and post-upload verification. This is intended for a representative game or
test payload; it does not turn the runner into a general unpinned file copier.

The runner never retries automatically and requires the process-cleanup ELF
immediately before launch. It takes a bounded post-run klog
snapshot, scopes it to the new eboot PID, rejects fatal signals, app crashes,
XO faults, duplicate or unrecognized kernel warnings, and any VM warning beyond
the one proven FW 5.50/raw-ELF baseline line. It requires a self-requested kernel `KillApp()` event
followed by `[AppMgr] All processes exited`, and asks ps5debug-NG to prove that
the exact launched PID no longer exists. SystemService removes a self-killed
process before `sceSystemServiceKillApp` can return, so stdout after that call
is intentionally not used as an oracle. On
timeout or a post-PASS safety failure it derives that PID from klog and asks
ps5debug-NG to kill only that process before returning failure.

The first FW 5.50 attempt stopped safely before registration: kernel evidence
showed that byte verification touched the execute-only VideoOut text page
before its permissions were changed. OpenAGC `290213c` performs verification
inside the short RWX window and restores RX on every exit. A second run reached
1,800 frames, but the kernel reported a teardown SIGSEGV and a `0x4000` VM
resource leak after the sample had printed PASS. OpenAGC `18011af` now uses the
hardware-proven teardown order (close VideoOut, then delete its equeue), and the
sample emits PASS only after swapchain, device, surface, and instance cleanup.
The corrected run completed those cleanup checkpoints, but returning from the
Prospero ELF entrypoint then jumped back into `main` (`RIP 0x4000bb`,
`main+0xbb`) and caused SIGSEGV. Subsequent `thr_exit` and libc `exit`
candidates completed Vulkan cleanup but left hbldr or the raw-ELF application
lifecycle incomplete; the libc run (`20260728T060157Z-swapchain-run1.log`)
left PID 145/app ID `0x16` owning a black screen. A guarded recovery payload
called `sceSystemServiceKillApp` for that app and restored the home screen,
which was confirmed visually. The Prospero sample now resolves its app ID with
`sceSystemServiceGetAppStatus`, requests SystemService termination after all
Vulkan cleanup, and keeps the main thread alive until the system completes it.
The termination helper is C11 `_Noreturn`; Prospero disassembly shows `main`
ending in the helper call followed by `ud2`, while both helper outcomes end in
sleep loops, so the candidate has no raw-loader return path.
The next run (`20260728T062155Z-swapchain-run1.log`) completed all 1,800 frames
and Vulkan cleanup. Its scoped klog recorded the self-requested `KillApp()`,
`All processes exited`, and shell focus restoration; exact-PID inspection found
no process and websrv remained responsive. The sole failure was a `0x4000` VM
resource warning, exactly matching OpenAGC's standalone multi-submit trailer
allocation. OpenAGC `1c0fb8f` now carves the 64-byte trailer from unused
`SceGnmDdid` space instead of allocating another 16 KiB VM resource.
The follow-up (`20260728T063200Z-swapchain-run1.log`) again completed 1,800
frames, Vulkan cleanup, the full kernel app-exit lifecycle, exact-PID absence,
and the bounded websrv response, but reproduced the same warning. This falsified
the trailer hypothesis. OpenAGC had restored the temporarily writable
execute-only VideoOut text range as read/execute; `0c22e06` now restores its
exact original execute-only protection so the kernel can coalesce the mapping.
The next gate (`20260728T063634Z-swapchain-run1.log`) reproduced the warning,
falsifying that mapping hypothesis as well. All 27 retained OpenAGC graphics
klogs contain the identical one-page warning. OpenAGC `4f66aa7` now balances
the remaining flip-event lifecycle explicitly—unregister event, close
VideoOut, then delete the still-live equeue.
The balanced-lifecycle run (`20260728T064111Z-swapchain-run1.log`) again
completed 1,800 frames, Vulkan cleanup, self-KillApp, exact-PID removal, and
shell restoration without a crash, but retained the same warning. The bounded
`vulkan_ps5_system_exit_probe` now isolates the raw-ELF/SystemService path: it
links only `kernel_sys`, `SceSystemService`, `unwind`, `c++abi`, `c++`, and `m`,
performs no Vulkan, OpenAGC, GPU, VideoOut, equeue, or custom-memory work, and
classifies either a clean teardown or exactly one baseline `0x4000` warning.
Its ELF SHA-256 is
`e585e74f872a4dfc7fa63910437b106843334666157672f9959c27558afe06a9`.
The bounded baseline run
(`20260728T064628Z-system-exit-probe-target.klog`) produced the exact same
single warning while completing self-KillApp, exact-PID removal, and the
post-run console probe. This proves the line is raw-ELF container bookkeeping,
not a Vulkan/OpenAGC/VideoOut leak. The diagnostic can be repeated only after a
fresh explicit console signal:

```sh
PS5_HOST=10.0.1.41 examples/run_fw550_system_exit_probe.sh
```

`vulkan_ps5_process_cleanup.elf` retains the proven recovery path and refuses
to act unless exactly one other `eboot.elf` exists. The balanced 1,800-frame
swapchain run plus the dependency-free baseline comparison close the FW 5.50
Milestone 4 hardware gate.

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

`vulkan_ps5_storage_image_probe` uses the same standard compute path with a
linear RGBA8 storage image and SPIR-V
`StorageImageWriteWithoutFormat`. Host execution verifies compilation,
descriptor encoding, and command recording; the FW 5.50 gate verifies an exact
4,096-pixel checkerboard. Run the bounded hardware gate with
`PS5_HOST=10.0.1.41 examples/run_fw550_storage_image.sh`.

`vulkan_ps5_robust_buffer_probe` requests `robustBufferAccess`, binds a
16-byte SSBO range backed by a larger allocation, and proves that an OOB read
returns zero while an OOB store leaves the guard word unchanged. The companion
`vulkan_ps5_robust_vertex_probe` uses sparse binding 3, shader location 5, and
an attribute offset beyond its eight-byte buffer; a zero fetch must produce an
exact 18,432-pixel blue triangle. Run their bounded FW 5.50 gates with
`PS5_HOST=10.0.1.41 examples/run_fw550_robust_buffer.sh` and
`PS5_HOST=10.0.1.41 examples/run_fw550_robust_vertex.sh`.

`vulkan_ps5_sample_rate_shading_probe` creates an ordinary 4x optimal-tiled
RGBA8 color attachment, requests `sampleRateShading`, and uses `gl_SampleID`
atomics to prove four invocations per covered pixel. The partial companion
uses `minSampleShading=0.5` without sample builtins and proves the two-iteration
rate independently. Their bounded gates are
`examples/run_fw550_sample_rate_shading.sh` and
`examples/run_fw550_partial_sample_rate_shading.sh`. Current 4x image exposure
is intentionally limited to single-layer RGBA8 color attachments without
sampled, transfer, or resolve usage until those paths are separately
qualified.

`vulkan_ps5_image_cube_array_probe` queries and requests `imageCubeArray`,
creates a standard 12-layer cube-compatible RGBA8 image and cube-array view,
and samples cube indices 0 and 1 through a `samplerCubeArray`. The bounded
`examples/run_fw550_image_cube_array.sh` gate requires an exact 9,216/9,216
red/green split across the 18,432 covered pixels. Cube-compatible sampled
images support both linear and optimal Vulkan tiling; the current one-mip
image limit remains unchanged.

The same option builds `vulkan_ps5_triangle_example`. It renders a solid-green
triangle into a mapped 256x256 linear RGBA8 attachment through an ordinary
render pass, waits for completion, invalidates the allocation, and verifies the
center, background, and green-pixel count. Its Prospero output is
`vulkan_ps5_triangle_example.elf`; generic-host execution likewise reports the
expected all-zero readback.

`vulkan_ps5_geometry_example` adds a standard geometry stage to that workload.
The geometry shader shrinks the input triangle by one half in each dimension,
so its mapped-memory oracle expects roughly one quarter of the ordinary
triangle coverage and cannot pass through a vertex-only path accidentally. The
host command regression records an indexed draw with the fused VS+GS primitive
record; its Prospero output is `vulkan_ps5_geometry_example.elf`. Two
independent FW 5.500.008 launches each produced exactly 4608 green pixels, so
the standalone geometry workload is hardware-qualified. The ICD now reports
`geometryShader = VK_TRUE` through both core feature-query forms and accepts it
through legacy `pEnabledFeatures` and `VkPhysicalDeviceFeatures2`, while still
rejecting unadvertised features. The sample queries and requests geometry
normally. Both seven-test host configurations and the Prospero build pass; the
feature-requesting ELF links with `-lunwind -lc++abi -lc++ -lm` and has SHA-256
`386aae854e1aaf504a750aa29904c491e35220d52c718c3bcf048f54de6803a4`.
Two independent bounded FW 5.500.008 runs produced exactly 4608 green pixels
each (`20260728T051424Z-geometry-run1.log` and
`20260728T051510Z-geometry-run1.log`). Both returned normally, bounded post-run
websrv checks confirmed the console remained responsive, and neither was
retried. The standard public `geometryShader` path is hardware-qualified.

`vulkan_ps5_tessellation_example` uses a three-control-point patch, level-two
TCS factors, and a TES that scales the evaluated triangle to 62.5 percent. Its
distinct mapped-memory coverage oracle exercises the shared factor/offchip
rings, ring descriptor table, fused Wave32 LS+HS and TES+NGG records, and
`DRAW_INDEX_AUTO`. Two safe black-target diagnostics first identified and fixed
missing command-buffer ring programming, then left unqualified offchip
patch-output reads as the only material difference from OpenAGC's passing
path. The basic hardware gate now uses OpenAGC's qualified constant-position
shader dataflow. Its Prospero ELF also cross-links with the required target
runtimes. Two independent FW 5.500.008 launches each produced exactly 7200
green pixels, hardware-qualifying the basic standalone workload. The later
patch-output-read qualification below completed the requirement for exposing
the core tessellation feature.

The qualified patch-output candidate derives its TCS/TES offchip layout words
from openagc-psbc API v5 metadata and OpenAGC's typed layout builder rather
than reusing the OpenAGC fixture constant. Pipeline creation also supplies the
adjacent TES module while compiling TCS and the adjacent TCS module while
compiling TES or TES+GS. The compiler links that non-executable interface
module so both separately emitted programs share one offchip location remap.
Host command-recording tests cover the linked compiler path and derived PM4
values; the public feature bit was kept disabled until the restored patch-output
sample passes the bounded hardware gate twice. The first bounded API v5 FW
5.500.008 run returned safely and left the console responsive, but its target
remained zeroed, so no retry was attempted.

The next host-qualified correction uses openagc-psbc API v6 to carry RADV's
pipeline-specific hull LDS byte requirement into OpenAGC. The tessellation
binder encodes that allocation in `SPI_SHADER_PGM_RSRC2_HS`, enabling the
separate LS-front/HS-back memory path used when a TCS reads VS outputs. This
state is covered by command-recording tests. Its first bounded FW 5.500.008 run
returned normally and left etaHEN websrv responsive, but the target was still
zeroed (`20260728T023553Z-tessellation-run1.log`). The runner did not retry it;
hull LDS allocation alone therefore does not qualify patch-output reads.

Post-failure comparison with Mesa exposed a gfx10.3 encoding detail in that
candidate: HS LDS bytes must first be rounded to a 1024-byte allocation and
then represented in the register's 512-byte units. OpenAGC now emits only the
resulting even field values, and the Vulkan command regression rejects a zero
or odd encoding. Its one bounded FW 5.500.008 run returned normally and left
etaHEN websrv responsive, but again produced a zeroed target
(`20260728T024632Z-tessellation-run1.log`). It was not retried. Correct LDS
rounding is required, but it is not the remaining VS-to-TCS dataflow fix.

The next locally verified candidate fixes that dataflow directly. RADV had
copied its monolithic VS+TCS same-invocation optimization into both shader-info
records, while openagc-psbc disabled it only for the separately emitted LS
front. The HS back consequently retained `load_per_vertex_input` operations;
standalone ACO compiled those unavailable temporary-VGPR values as zero, which
made the TCS write zero positions to the offchip ring. openagc-psbc now forces
both halves through the common LDS ABI and rejects compilation if an HS input
survives lowering. Host inspection confirms the HS loads are `load_shared`, all
seven ICD tests pass, and the Prospero ELF links with `-lunwind -lc++abi -lc++
-lm`. Its one bounded FW 5.500.008 run returned normally and left etaHEN
websrv responsive, but the target remained zeroed
(`20260728T030535Z-tessellation-run1.log`). It was not retried. Correct
LS-to-HS LDS dataflow is retained, but the remaining failure is downstream in
HS offchip storage, tessellation ring state, or TES consumption; feature
advertisement remains disabled.

The next materially distinct qualification candidate adds a standard Vulkan
storage buffer to TCS. Each hull invocation writes an independent execution
marker and the VS position it read from LDS. After the existing bounded fence
wait, the sample invalidates and checks that mapped buffer before reporting the
image oracle. This separates LS-to-HS execution/LDS failures from downstream
HS-offchip/TES failures without exposing a private driver API. Host pipeline
creation, command recording, all seven ICD tests, and the Prospero link passed.
Its first and only FW 5.500.008 launch froze graphics until Shell UI restarted
(`20260728T031733Z-tessellation-run1.log`). The ps5debug-NG kernel stream
identified PID 129 with an SQC-data read protection fault at unmapped VA
`0x0000000200000000`; the faulting wave reported `XNACK_ERROR MEMVIOL`. No
test process remained after recovery, so no unrelated process was killed and
the candidate was not retried.

ACO inspection showed that the separately compiled HS uses its indirect
descriptor-set-table pointer in `s14`. openagc-psbc API v7 now reports that
indirect pointer distinctly from ordinary direct set pointers, and the ICD
allocates a GPU-visible array of bound-set low addresses and writes the array
address to the compiler-selected register before drawing. The ICD rejects the
table or any bound-set pointer outside gfx1013's `0x2_xxxxxxxx` aperture. The
command test requires the pointer and bound set-1 entry to be nonzero and
matches the exact compiler-selected register/value pair in PM4. Both seven-test
host configurations pass and the Prospero link
includes `-lunwind -lc++abi -lc++ -lm`. The corrected ELF has SHA-256
`9be96734ae5b1643d9b1f408101cc345bb0bb7291491ed8fbdc989ab974285cc` and
completed its first bounded FW 5.500.008 launch successfully
(`20260728T034030Z-tessellation-run1.log`). The storage-buffer oracle reported
all three hull invocation markers (`0x48530000`, `0x48530001`, and
`0x48530002`) and the exact three input control-point positions; the image
oracle reported exactly 7200 green pixels. The runner returned normally, and
no retry was attempted. A second independent fresh-console run reproduced all
hull markers, positions, and exactly 7200 green pixels
(`20260728T034211Z-tessellation-run1.log`); the console remained responsive.
The restored patch-output path is hardware-qualified at this scope. The ICD now
returns `tessellationShader = VK_TRUE` from both core feature-query forms and
accepts it from `pEnabledFeatures` or `VkPhysicalDeviceFeatures2`, while still
rejecting unadvertised core features. Lifecycle regressions cover the Features2
success path and an unsupported-feature rejection. The standalone sample now
queries and requests tessellation explicitly. Both host configurations pass all
seven tests, and its rebuilt Prospero ELF links with `-lunwind`, `-lc++abi`,
`-lc++`, and `-lm` and has SHA-256
`a1fce3414f4fadac09ff10148d76302bac504ac3befd119e059bd3330877d30d`.
Its one bounded hardware smoke passed every hull marker and copied-position
check but produced an all-zero image
(`20260728T034904Z-tessellation-run1.log`). The application returned and the
console remained responsive; no retry was attempted. Apart from the final
image oracle, this log is identical to the immediately preceding passing log.
The result exposes nondeterminism after TCS execution in the
offchip-to-TES/raster path, so public feature exposure is not considered stable
until that path is corrected and the repeated hardware gate passes again.

The next diagnostic keeps the interface application-neutral by extending the
existing storage-buffer binding to TES. The tessellated vertex with
`gl_TessCoord.x` equal to one writes marker `0x54455300` and copies all three
offchip control-point positions. A missing marker identifies TES launch or
factor-ring state, a marker with incorrect positions identifies offchip reads,
and a passing TES probe with a zero image identifies rasterization. Both host
configurations pass all seven tests, host execution reaches the expected
no-GPU oracle, and the Prospero ELF links with `-lunwind`, `-lc++abi`, `-lc++`,
and `-lm`. Its SHA-256 is
`b3e239c757996b7b8296719d461415913e9f1475b601553f28dd5aa06ac65c6e`.
Its one bounded FW 5.500.008 run returned normally and left the console
responsive (`20260728T035640Z-tessellation-run1.log`). The hull probe still
passed, and TES wrote marker `0x54455300`, so the evaluation stage and factor
ring are active. TES copied zero for every component of all three offchip
control points and the image remained black. This excludes downstream
rasterization and localizes the correction to HS offchip stores or the matching
TES offchip address/layout ABI. No retry was attempted.

OpenAGC commit `6406c9b` corrects the resulting ring-capacity mismatch. The old
public profile allocated one 8K-dword (32 KiB) offchip buffer and programmed
`VGT_HS_OFFCHIP_PARAM = 0`, despite gfx1013 being able to retain four offchip
workgroups per CU across four shader engines, two shader arrays per engine, and
five CUs per array. The new application-neutral profile provisions all 160
buffers in a 5 MiB offchip ring, programs the encoded buffer count as `159`,
and uses Mesa's 120 KiB (`0x1e000`) factor-ring size. OpenAGC now rejects a
ring whose storage is smaller than its encoded buffering/granularity profile.
Both Vulkan host configurations pass all seven tests, the Prospero build passes
and links with `-lunwind -lc++abi -lc++ -lm`, and the resulting tessellation ELF
has SHA-256
`316ee53df2a1b29d7dcd1c5f1c4adb3cfe0d0f07bb66f2ea41cb1d88eda9e09b`.
This candidate still requires a fresh-console, single bounded hardware run;
the driver does not treat the nondeterministic offchip path as qualified yet.

Two independent FW 5.500.008 runs of that full-ring candidate passed every
oracle (`20260728T043915Z-tessellation-run1.log` and
`20260728T044035Z-tessellation-run1.log`). In both runs all three hull markers
and copied LDS positions matched, TES wrote marker `0x54455300` and copied the
exact three offchip control-point positions, and the image contained exactly
7200 green pixels. Both processes returned normally, bounded post-run websrv
checks confirmed that the console remained responsive, and neither run was
retried. The repeated hardware gate is closed: the offchip correction and
public `tessellationShader` exposure are hardware-qualified at this scope.

Run the advanced stages one at a time. The default is one run so a new packet
path is never repeated automatically. After each first pass, invoke that stage
once more to collect the second independent qualification log. Console
reachability, FTP upload, and HTTP launch operations all use bounded timeouts.
Advanced-stage HTTP launches allow 60 seconds for runtime pipeline compilation
while retaining the one-run default:

```sh
PS5_HOST=10.0.1.41 examples/run_fw550_advanced_stages.sh geometry
PS5_HOST=10.0.1.41 examples/run_fw550_advanced_stages.sh geometry
PS5_HOST=10.0.1.41 examples/run_fw550_advanced_stages.sh tessellation
PS5_HOST=10.0.1.41 examples/run_fw550_advanced_stages.sh tessellation
```

`vulkan_ps5_indexed_textured_example` binds an interleaved position/UV vertex
buffer, a UINT16 index buffer containing `{1,2,3}` after a decoy vertex, and a
bilinear clamp sampler over a 2x2 RGBA8 image. Its readback oracle requires
triangle coverage, fully opaque sampled pixels, at least 16 distinct colors,
an interior center sample, and untouched background corners.

`vulkan_ps5_depth_example` combines a mapped linear RGBA8 target with an
OpenAGC-laid-out optimal D32+S8 attachment. One near green triangle must occlude
an overlapping far red triangle while a separate far red triangle passes over
the initialized depth value. Passing fragments replace stencil with `0x5a`.
Its oracle checks exact interior colors, coverage, raw clear/near/far D32 words,
and requires the S8 write count to equal final green-plus-red coverage.
Generic-host execution intentionally reports unchanged memory because that
backend does not execute GPU commands.

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
The final expanded `20260728T003956Z` gate passed all six workloads twice:
query returned 18,432 samples matching 18,432 mapped pixels, combined D32+S8
produced identical `54145/12288/9830` raw-depth counts and 22,118 stencil writes
matching color coverage, and MRT produced 18,432 pixels in both targets. The
full investigation and retained-log map are recorded in
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
creation, including independent specialization data for fused stages. Geometry
and tessellation draws now accept the fused compiler records and metadata;
tessellation uses device-owned OpenAGC rings registered through the FW driver
and restores typed depth/stencil state after binding. Both optional Vulkan
feature bits remain disabled until their standalone pipelines are repeatedly
qualified on hardware. Compute dispatch and a no-input triangle draw now emit real
gfx1013 `DISPATCH_DIRECT` and `DRAW_INDEX_AUTO` packet sequences.
Uniform/storage-buffer descriptor sets are stored through standard Vulkan
updates, encoded into GPU-visible OpenAGC tables, and patched into the compiler-
selected user-SGPR immediately before compute dispatch or graphics draws.
Graphics buffer descriptors are covered by a TCS command-recording test so the
tessellation qualification workload can use a standard storage-buffer oracle.
Indexed draws bind standard
Vulkan vertex/index buffers, build per-draw GPU-visible gfx1013 vertex tables,
and emit `DRAW_INDEX_2` for UINT16 or UINT32 indices. Combined and separate
sampled-image/sampler descriptors for linear RGBA8/BGRA8 images are encoded
through OpenAGC and bound to compiler-selected graphics SGPRs. Linear image
allocation and subresource layouts use the gfx1013-required 256-byte row pitch;
the sampled-image path is repeatedly hardware-qualified. Dynamic buffer
offsets and optional sparse, protected, external-handle, multiview, and YCbCr
features remain unavailable. Timeline semaphores are available with finite
host waits and completion-ordered queue values.

On Prospero, `vkCreateDevice` reaches OpenAGC initialization, which now keeps
the FW-specific GPU process-authorization preparation inside its `/dev/gc`
backend. Standalone Vulkan applications do not include or call the OpenAGC
hardware-test credential header.

The initial graphics render-pass path supports one single-sampled linear color
attachment or up to eight MRT color attachments, an optional OpenAGC-laid-out
optimal depth/stencil attachment, one
inline subpass, load/don't-care operations, fixed full-range viewport/scissor
state, fill rasterization without culling, and static per-target blending and
color write masks. Vulkan blend factors and operations, separate alpha state,
four blend constants, and up to eight independent attachment records translate
to OpenAGC's typed gfx1013 blend state before baseline, geometry, indirect, and
tessellation draws. SRC1 factors are accepted for attachment zero when
`dualSrcBlend` is enabled; later attachments reject them as required.
The bounded `vulkan_ps5_dual_src_blend_probe` uses a white primary output and
green source-index-1 output, then requires exactly 18,432 opaque green pixels.
Two consecutive FW 5.50 runs passed and self-exited cleanly; only the known
`amount=0x4000` baseline VM warning appeared.
The standalone `vulkan_ps5_independent_blend_probe` renders the same
triangle to two attachments: target zero must remain opaque green with blending
disabled, while target one must become half-intensity magenta through
constant-color/alpha factors. The 8-bit UNORM tie may encode as `0x7f7f007f`
or `0x80800080`; no other value is accepted. Its bounded runner requires
the shared self-kill lifecycle. Hardware diagnostics identified OpenAGC's
former unconditional color-target blend bypass; OpenAGC `d2522fa` now derives
the correct policy from the number type. Both the internal-path and public
query/request FW 5.50 gates passed with 18,432 pixels per target, exact
`0x80800080` target-one output, clean process exit, and only the established
0x4000 baseline VM warning. `independentBlend` is advertised through legacy
and Features2 paths; the public-path ELF SHA-256 is
`e42014fcab89df6001555faecd6a2c4a0d05edb87d9d7bcd011c62ca0caa6a99`.
Static `logicOpEnable` pipelines map all 16
core Vulkan operations to OpenAGC's typed gfx1013 ROP3 state and suppress
attachment blending while active. Disabled logic state restores COPY. The
bounded `vulkan_ps5_logic_op_probe` XORs the green fragment value over mapped
`0x55aa33cc` destination pixels and accepts only exact `0xaaaacccc` coverage,
proving that both source and destination participate. Both the internal-path
and public query/request FW 5.50 gates passed with exact readback, clean
target-process self-exit, and only the established 0x4000 baseline VM warning.
`logicOp` is advertised and accepted through legacy and Features2 paths; the
public-path ELF SHA-256 is
`aee2fa93057571ee294862c822c11f1c4ca924b55938c315e51d93968cae21e1`.
Static and
`VK_DYNAMIC_STATE_DEPTH_BIAS`
pipelines translate constant, clamp, and slope factors to OpenAGC's typed D16
or D32 depth-bias state for every graphics draw path. The standalone
`vulkan_ps5_depth_bias_clamp_probe` applies a deliberately oversized constant
bias with a 0.125 clamp and requires exact D32 depth shifts from 0.25/0.75 to
0.375/0.875. Both the internal-path and public query/request FW 5.50 gates
passed with exact color, D32, and stencil counts, clean process exit, and only
the established 0x4000 baseline VM warning. `depthBiasClamp` is advertised and
accepted through legacy and Features2 paths; the public-path ELF SHA-256 is
`f2da4d9bab0030cdfe342ed8abc42e03601f5f66fcdb47d39ce761ad42702244`.
Every frame uses gfx1013's Vulkan zero-to-one depth clip convention
and the matching viewport transform (`ZSCALE=1`, `ZOFFSET=0`).
Static `depthClampEnable` is accepted for every graphics draw path and disables
near/far Z clipping through OpenAGC while retaining the Vulkan convention. The
standalone `vulkan_ps5_depth_clamp_probe` requires a negative-Z green triangle
to survive at exact D32 zero and a normal red control triangle at exact 0.25.
The original depth sample now uses 0.25/0.75 shader depth so its established
oracle remains unchanged under correct Vulkan clip space. The corrected FW 5.50
gate passed exact color, raw D32, and stencil oracles with a matching self-exit;
`depthClamp` is advertised and accepted through legacy and Features2 paths.
Line and point polygon modes are also accepted through OpenAGC's typed gfx1013
raster state. The bounded `vulkan_ps5_fill_mode_non_solid_probe` draws a green
wireframe and three separate red points, rejecting filled interiors; its FW
5.50 gate passed and `fillModeNonSolid` is advertised. Point-list pipelines now
use OpenAGC's typed topology and primitive-size state. `largePoints` is exposed
through legacy and Features2 query/enable paths with range `[1, 64]` and
granularity `0.125`; the bounded FW 5.50 probe deterministically qualified
8-, 16-, and 32-pixel shader point sizes. Line-list and line-strip pipelines
support static and `VK_DYNAMIC_STATE_LINE_WIDTH` state. `wideLines` is exposed
through both feature paths with range `[1, 64]` and granularity `0.125`; its
bounded FW 5.50 probe qualified exact 8-, 16-, and 32-pixel line coverage.
`VK_EXT_shader_demote_to_helper_invocation` is enumerated, reports
`shaderDemoteToHelperInvocation = VK_TRUE`, and accepts a normal device request.
Its bounded shader gate demotes every even-X invocation, requires exactly
32,768 suppressed writes, and verifies 2,048 surviving pixels whose derivative
depends on the post-demote helper value; the public FW 5.50 run passed.
Core `shaderClipDistance` is advertised and accepted through legacy and
Features2 paths. Its bounded vertex-shader gate writes `gl_ClipDistance[0]`
from clip-space X and requires exactly 9,216 retained green pixels, a zero left
sample, and a green right sample; the public FW 5.50 run passed.
Core `shaderCullDistance` is likewise advertised and accepted. Its bounded
vertex-shader gate assigns an all-negative distance to a left triangle and an
all-positive distance to a right control, requiring exactly 4,608 retained
green pixels with a zero left sample and green right sample.
Static depth compare/write
and front/back stencil state are
translated to typed OpenAGC draw state. Begin/end render pass translate layouts to OpenAGC
resource transitions, emit the qualified gfx1013 frame prologue, bind attachment
addresses, and restore host-readable cache state after drawing. Depth/stencil
clears remain unavailable; the first hardware gate initializes mapped direct
memory and uses `LOAD`.

Occlusion query pools use typed GPU-visible OpenAGC buffers and native query
commands; Vulkan no longer records ZPASS or availability packets itself.
Reset is command-ordered, end-query publishes a separate EOP
availability label, and `vkGetQueryPoolResults` supports 32/64-bit values,
availability, partial results, and bounded waits. Repeated FW 5.50 runs matched
the exact 18,432-sample result to independent mapped color coverage, so
`occlusionQueryPrecise` is advertised and the query sample requests it and
records `VK_QUERY_CONTROL_PRECISE_BIT`. Timestamps remain disabled pending
hardware qualification.
`VK_EXT_host_query_reset` is advertised with `hostQueryReset = VK_TRUE` and
flushes the selected query slots after a host-side reset.
