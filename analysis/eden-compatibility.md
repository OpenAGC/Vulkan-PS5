# Eden Compatibility Profile

This is the live Milestone 6 gap matrix for upstream `../eden-ps5` revision
`612409c7ba` (the checkout head also contains the PS5 plan commit
`b5cdae421b`). The Eden checkout was inspected read-only; its existing deleted
`AGENTS.md`/`CLAUDE.md` and untracked `.serena/` state were not changed.

Run the profile probe in reporting mode through CTest. `--strict` also exits
successfully now that every hard ICD startup requirement passes:

```sh
ctest --test-dir build -R vulkan_ps5.eden_profile_report --output-on-failure
build/vulkan_ps5_eden_profile_test --strict
```

## Startup suitability

| Requirement | Eden requirement | Current evidence | State |
| --- | --- | --- | --- |
| API | Vulkan 1.1 or newer | ICD reports Vulkan 1.1 | Pass |
| Device extensions | `VK_EXT_vertex_attribute_divisor`, `VK_EXT_shader_demote_to_helper_invocation`, `VK_KHR_driver_properties`, `VK_KHR_sampler_mirror_clamp_to_edge`, `VK_KHR_shader_float_controls` | All five are enumerated, queryable, and accepted at device creation | Pass |
| Core/Features2 | 29 mandatory feature bits | All 29 mandatory feature bits are true | Pass |
| Limits | UBO range 65,536; 16 viewports; 8 color attachments; 8 clip distances | All four exact minima are reported | Pass |
| Queues | At least one graphics queue; present support when a surface exists | One universal graphics/compute/transfer queue is reported; the WSI family supports present | Pass |
| Swapchain | `VK_KHR_swapchain` when a surface is supplied | Enumerated and hardware-qualified in Milestone 4 | Pass |
| PS5 surface | A standard surface must be supplied by the PS5 frontend | Eden currently has no Prospero surface/build integration and treats headless as no surface | Gap outside ICD |

The sampler mirror-clamp contract is hardware-qualified and counted as
supported. `vulkan_ps5_mirror_clamp_probe` samples `(-0.5, -0.5)` and
requires the mirror-once gray texel instead of the red edge-clamp texel. The
one-shot runner has host-tested clean and crash paths, exact-PID cleanup, and
bounded warning handling. Both the internal-path and extension-enabled FW 5.50
runs produced 18,432 gray pixels with exact center `0xff808080`, clean
SystemService self-exit, no stale process, and clean target-only klog.
`VK_KHR_sampler_mirror_clamp_to_edge` is enumerated and accepted at device
creation. The public-path Prospero ELF SHA-256 is
`6b591dfe79c69f32cc9efdb641ab686183b0c7c0e032df7f3892f6e3357ce78f`.

The vertex-divisor contract is implemented, hardware-qualified, and advertised.
openagc-psbc API v8 passes input rate and divisor state into RADV's
gfx1013 lowering, and Vulkan pipeline creation consumes
`VkPipelineVertexInputDivisorStateCreateInfoEXT` with divisor-one defaults and
nonzero-divisor validation. Compiler and pipeline regressions pass on host and
both components build for Prospero. Legacy EXT and promoted properties report
the compiler's `UINT32_MAX` nonzero range. Both feature-query paths expose
nonzero instance-rate divisor support while zero divisor and
nonzero-first-instance support remain false; device creation accepts the former
and rejects the latter. The probe and one-shot runner require four overlapping
instances to resolve to an exact-white triangle only when divisor two is
honored. Its
runner safety regression and both full 25-test host suites pass. Both the
internal-path and extension/feature-enabled FW 5.50 gates produced 18,432
exact-white pixels with center `0xffffffff`, clean SystemService self-exit, no
stale process, and clean target-only klog. The public-path Prospero ELF SHA-256
is `5647b97d9ad8944028c4e242c49503a36f307ecc9ff603d765aec2f56b0c1503`.

Sampler anisotropy is now public and hardware-qualified through both legacy and
Features2 feature-query/device-enable paths. The internal and public FW 5.50
contrast gates each reported `linear=9830/127/63 aniso=9830/127/5`, followed by
clean exact-PID exit and clean target-only klog. The public-path Prospero ELF
SHA-256 is
`28319bef31f227ea45b9aacc35138e10b9cb136b88dbba350bc2a562a16c49b9`.

The automated probe currently reports:

```text
eden-profile: extensions=0 features=0 limits=0 queues=0 total=0
```

The last feature, `multiViewport`, is hardware-qualified through two static
viewport/scissor slots selected by `gl_ViewportIndex`. Both bounded FW 5.50
runs produced exactly 9,216 green and 9,216 red pixels, self-exited, and left
only the known `amount=0x4000` baseline warning. The corresponding host and
ASAN/UBSAN suites pass 39/39 with VVL-clean coverage. This completes the ICD
startup profile; the PS5 surface/build row above remains frontend integration.

The `independentBlend` contract is implemented and hardware-qualified. Graphics
pipelines translate distinct per-attachment enable state, non-dual-source
factors, all five core blend operations, separate alpha equations, write masks,
and constants into OpenAGC's typed gfx1013 blend state. Every baseline,
geometry, indirect, and tessellation draw restores that state. The exact PM4
regression uses one disabled full-mask target and one enabled GB-only target,
requiring distinct control words, target mask `0x6f`, and all four constants.
Both normal and ASAN/UBSAN suites pass 25/25 and the Prospero build passes.
The standalone MRT gate now requires target zero to retain opaque green while
target one resolves to half-intensity magenta through constant-color/alpha
factors, followed by the shared SystemService lifecycle. Since exact 0.5 is a
tie when converted to 8-bit UNORM, the gate accepts only `0x7f7f007f` or
`0x80800080`.
Its runner covers clean, fatal, NUL-containing, PID-reuse, and exact-identity
paths. Hardware diagnostics showed full-color output even when blending was
moved to target zero, identifying OpenAGC's unconditional
`CB_COLOR_INFO.BLEND_BYPASS` as the cause. OpenAGC `d2522fa` now derives clamp,
bypass, simple-float, and round-mode policy from the target number type. The
internal-path and final public query/request FW 5.50 gates both produced 18,432
pixels on each target with exact target-one `0x80800080`, clean process exit,
and only the established 0x4000 baseline VM warning. `independentBlend` is now
advertised and accepted through legacy and Features2 paths. The public-path Prospero ELF SHA-256 is
`e42014fcab89df6001555faecd6a2c4a0d05edb87d9d7bcd011c62ca0caa6a99`.

The `dualSrcBlend` contract is implemented and hardware-qualified. Legacy and
Features2 paths advertise, query, and request the feature normally. Pipeline
creation maps all core SRC1 color/alpha factors, rejects SRC1 on attachments
after MRT0, and passes complete dual-source context into `openagc-psbc`. The
compiler uses explicit source-index-1 color mapping, native gfx1013 MRT0/MRT1
32-bit ABGR exports, and Oberon's DB dual-export bit; OpenAGC disables RB+
dual-quad mode and all SX blend optimization while SRC1 is active. The bounded
probe multiplies a white primary output by a green source-index-1 output. Two
consecutive FW 5.50 runs produced exactly 18,432 opaque green pixels with center
`0xff00ff00`, self-exited cleanly, and logged only the established
`amount=0x4000` baseline VM warning (`20260728T152837Z` and
`20260728T152900Z`).

The `shaderStorageImageWriteWithoutFormat` contract is implemented and
hardware-qualified. Vulkan advertises storage-image support only for linear
`VK_FORMAT_R8G8B8A8_UNORM`, accepts storage-image descriptor writes, and emits
gfx1013 image descriptors for compute and graphics resource tables. The
standalone compute probe compiles SPIR-V with
`StorageImageWriteWithoutFormat`, requests the feature normally, and writes a
64x64 green/magenta checkerboard through `imageStore`. Both bounded FW 5.50
runs verified all 4,096 pixels exactly, self-exited cleanly, and logged only
the established `amount=0x4000` baseline VM warning
(`20260728T154623Z` and `20260728T155150Z`). The Prospero ELF SHA-256 is
`5234ca8640902545ea1c6c55bfe2f503365c3119678fb9ad4d030c51d96ed39a`.

The `logicOp` contract is implemented and hardware-qualified. Every one of the
16 core `VkLogicOp` values maps to OpenAGC's typed
gfx1013 ROP3 truth table. Active logic state suppresses per-target blending,
while disabled state restores COPY; baseline, indexed, indirect, geometry, and
tessellation draw paths all restore the pipeline state. Pipeline rejection and
exact `CB_COLOR_CONTROL=0x00660010` XOR regressions pass in both 25/25 host
configurations. The bounded probe fills mapped RGBA8 destination pixels with
`0x55aa33cc`, draws green through XOR, and requires exact `0xaaaacccc` triangle
coverage plus unchanged background before the shared matching-self-kill exit.
The internal-path and final public query/request FW 5.50 runs both produced
18,432 XOR pixels, 47,104 unchanged pixels, the exact center value, and clean
target-process self-exit. Target-only klogs contained only the established
0x4000 baseline VM warning. `logicOp` is now advertised and accepted through
legacy and Features2 paths. The public-path Prospero ELF links the required
runtime set and has SHA-256
`aee2fa93057571ee294862c822c11f1c4ca924b55938c315e51d93968cae21e1`.

The `depthBiasClamp` contract is implemented and hardware-qualified. Static
pipelines and `VK_DYNAMIC_STATE_DEPTH_BIAS` both preserve
constant, clamp, and slope factors; `vkCmdSetDepthBias` records command-local
dynamic values. Baseline, indexed, indirect, geometry, and tessellation paths
enable front/back polygon offset and emit OpenAGC's typed D16/D32 format,
clamp, slope, and constant registers. Exact PM4 and pipeline regressions pass
in both 25/25 host configurations. The standalone bounded probe supplies an
oversized constant bias with clamp 0.125 and requires raw D32 depth to move
from 0.25/0.75 to exact 0.375/0.875 while preserving the established color and
stencil decisions. Its runner covers matching-self-kill, NUL, later-PID, fatal,
and exact-identity paths. The internal-path and final public query/request FW
5.50 runs both produced exact green 12,288, red 9,830, raw D32
54,145/12,288/9,830, and stencil 22,118 counts, followed by clean process exit.
Target-only klogs contained only the established 0x4000 baseline VM warning.
`depthBiasClamp` is advertised and accepted through legacy and Features2 paths.
The public-path Prospero ELF links the required runtime set and has SHA-256
`f2da4d9bab0030cdfe342ed8abc42e03601f5f66fcdb47d39ce761ad42702244`.

The `depthClamp` contract is implemented and advertised. Every graphics frame
uses OpenAGC's exact `0x00080000` clip-control mask and `ZSCALE=1`, `ZOFFSET=0`
viewport transform for Vulkan's zero-to-one
depth convention; static depth-clamp pipelines
use `0x0c080000` to additionally disable near and far Z clipping on baseline,
indexed, indirect, geometry, and tessellation draws. The established depth
sample shader moved from -0.5/0.5 to 0.25/0.75 so its existing raw D32 oracle
remains valid. Exact PM4 and pipeline regressions pass in both 24/24 host
configurations. The bounded probe distinguishes clamping from clipping by
requiring a negative-Z green triangle at exact D32 zero, plus a normal red
control triangle at exact 0.25, and uses the shared matching-self-kill runner.
The hardware-tested Prospero ELF links `-lunwind -lc++abi -lc++ -lm` and has
SHA-256
`659590336c8030c7ae118210931ac8e0ee4dac3d455c888e53d22a09cd2751b9`.
The first bounded FW 5.50 run produced the expected color and stencil coverage,
completed SystemService exit, and left no stale process, but revealed that the
legacy OpenAGC viewport still applied a 0.5/0.5 depth remap after Vulkan clip
control was enabled. OpenAGC `c0dd5b4` adds an explicit zero-to-one viewport
mode and the command regression locks its exact values. The corrected bounded
FW 5.50 run on 2026-07-28 passed exact green/red/raw/stencil coverage, completed
the matching SystemService self-exit, left no process, and produced only the
known single `amount=0x4000` baseline VM warning. Legacy and Features2 queries
now report `depthClamp`; both device-create paths accept a normal request. The
post-promotion Prospero ELF is
`bcbfa074bb504ceabf352e6ecbdb1f45f112dfef70faea057d41a1eb82a9c947`.

The `fillModeNonSolid` contract is implemented and hardware-qualified for
one-pixel lines and points. Vulkan line and point polygon modes map through
OpenAGC's typed raster helper, which owns the exact gfx1013 dual-mode and
front/back primitive encodings while preserving depth-bias and other raster
state. Pipeline tests accept both core modes and reject the unsupported NV
rectangle mode; command tests require exact `PA_SU_SC_MODE_CNTL` values `0x128`
and `0x008`. The bounded FW 5.50 probe rendered a 230-pixel green wireframe and
exactly three red points with empty interiors, completed matching SystemService
self-exit, left no process, and produced only the known single `amount=0x4000`
baseline VM warning. The initial internal-path ELF SHA-256 was
`47a15536779e194f56bb20bb8a841a92fc0ebcaf05247f4c9fab95bc1ec988e1`.
Legacy and Features2 queries now report the feature, and both device-create
paths accept a normal request. The rebuilt public-path probe queried and requested
the advertised feature, reproduced the exact 230/3 oracle and clean lifecycle,
and has Prospero ELF SHA-256
`fd2dd48dddd46cd2519bd06fb5b9dacb6bc394658a1efc9d626b2258c9cdeeb3`.

The `largePoints` contract is implemented and hardware-qualified. Point-list
pipelines map to OpenAGC's typed gfx1013 point primitive, while OpenAGC
`949ca76` owns the exact primitive-size register packet. Vulkan reports
`pointSizeRange = [1, 64]` and `pointSizeGranularity = 0.125`, exposes the
feature through legacy and Features2 queries, and accepts both device-create
paths. Pipeline tests cover accepted point lists and retained rejection of
unsupported line topology; command tests require primitive type 1 and the
complete point-size PM4 sequence. Both normal and ASAN/UBSAN suites pass 26/26,
and the full Prospero build links the required runtime set. The first bounded
internal run aborted before GPU submission when openagc-psbc rejected a
function-local array `store_deref`; an equivalent ternary shader expression
compiled successfully without a compiler change. The corrected internal gate
and final public gate each produced exact 8px=64, 16px=256, and 32px=1024
coverage, correct center color, clean SystemService self-exit, and no stale
process. The public run log is
`20260728T115744Z-large-points-run1.log`; target klog contained only the known
single `amount=0x4000` baseline VM warning. The public-path Prospero ELF
SHA-256 is
`439a18445742d30595b1a2e850d5e5370c8e38fc8c55a9beb09b634d3fb9130f`.

The `wideLines` contract is implemented and hardware-qualified. Line-list and
line-strip input topologies map to OpenAGC primitive types 2 and 3. Static
pipeline widths and command-buffer-local `VK_DYNAMIC_STATE_LINE_WIDTH` both
feed OpenAGC's typed primitive-size packet; dynamic state must be set before a
draw and is cleared on command-buffer begin/reset. Vulkan reports
`lineWidthRange = [1, 64]` and `lineWidthGranularity = 0.125`, exposes the
feature through legacy and Features2 queries, and accepts both device-create
paths. Pipeline tests cover both line topologies, ignored static width for a
dynamic pipeline, and invalid range rejection. Command tests require exact
line primitive and 8px=`0x40`, 16px=`0x80`, and 32px=`0x100` register values.
Both normal and ASAN/UBSAN suites pass 27/27, and the full Prospero build links
the required runtime set. The internal gate
(`20260728T120753Z-wide-lines-run1.log`) and final public query/request gate
(`20260728T120940Z-wide-lines-run1.log`) each produced exact 1,024/2,048/4,096
coverage, correct center color, clean SystemService self-exit, and no stale
process. Target-only klogs contained only the known single `amount=0x4000`
baseline VM warning. The public-path Prospero ELF SHA-256 is
`25db7763fd45e5494067dbcf83ed16884fcfe9a6b93958b2a9d3f3e4a63fb109`.

The `shaderDemoteToHelperInvocation` contract is implemented and
hardware-qualified. `VK_EXT_shader_demote_to_helper_invocation` is enumerated,
Features2 reports the feature, and device creation accepts both the extension
and feature request. The fragment probe demotes every even-X lane, checks that
all 32,768 such invocations suppress framebuffer output, and makes surviving
lanes depend on the demoted helper value through `dFdx`. The exact derivative
oracle is confined to a 64-by-64 region within one rasterized primitive; this
avoids treating the implementation-dependent helper set along the clipped
fullscreen triangle's internal seam as a language failure. Both 28/28 host
suites, runner safety coverage, and the complete Prospero build pass. The
internal gate (`20260728T122309Z-shader-demote-run1.log`) and final public
extension/Features2 gate (`20260728T122623Z-shader-demote-run1.log`) each
reported exact `green=2048 blue=30720 demoted=32768`, matching SystemService
self-exit, no stale process, and only the known single `amount=0x4000` baseline
warning. The public Prospero ELF SHA-256 is
`f9980eb6bcf1dbf96bc587fae7dd2e43be84dddfa2a8b13ac6ce6ba22e4d7327`.

The core `shaderClipDistance` contract is implemented and hardware-qualified.
Legacy and Features2 queries report it, and device creation accepts the core
feature request. The vertex probe writes `gl_ClipDistance[0] = position.x`, so
the negative-X half of an otherwise 18,432-pixel triangle must be clipped.
Both 29/29 host suites, runner safety coverage, and the complete Prospero build
pass. The internal gate
(`20260728T123525Z-shader-clip-distance-run1.log`) and final public legacy
query/request gate (`20260728T123848Z-shader-clip-distance-run1.log`) each
reported exact `green=9216 left=00000000 right=ff00ff00`, matching
SystemService self-exit, no stale process, and only the known single
`amount=0x4000` baseline warning. The public Prospero ELF SHA-256 is
`64a4246ff57161364aa84cacb9377fe42b0e886ffce61851ab9329c88c163a31`.

The core `shaderCullDistance` contract is implemented and
hardware-qualified. Legacy and Features2 queries report it, and device
creation accepts the core feature request. The vertex probe draws two
disjoint 4,608-pixel triangles, assigning `-1.0` to every cull-distance value
of the left primitive and `1.0` to the right control. Both 30/30 host suites,
runner safety coverage, and the complete Prospero build pass. The internal
gate (`20260728T124816Z-shader-cull-distance-run1.log`) and final public legacy
query/request gate (`20260728T124948Z-shader-cull-distance-run1.log`) each
reported exact `green=4608 left=00000000 right=ff00ff00`, matching
SystemService self-exit, no stale process, and only the known single
`amount=0x4000` baseline warning. The public Prospero ELF SHA-256 is
`82ffa08623bf7632635c0009f51b439f4ae861d699c5b052cebc9bf1343dcabf`.

The core `shaderImageGatherExtended` contract is implemented and
hardware-qualified. Legacy and Features2 queries report it, and device
creation accepts the core feature request. The fragment probe executes
`textureGatherOffsets` with four constant offsets over a checkerboard; the
offsets make all four gathered red components select the same red texel, so
the only valid covered result is opaque white. Both 31/31 host suites, runner
safety coverage, and the complete Prospero build pass. The corrected internal
gate (`20260728T125800Z-shader-image-gather-run1.log`) and final public legacy
query/request gate (`20260728T130045Z-shader-image-gather-run1.log`) each
reported exact `covered=18432 center=ffffffff offsets=4`, matching
SystemService self-exit, no stale process, and only the known single
`amount=0x4000` baseline warning. The public Prospero ELF SHA-256 is
`de628e54eb7929f484715b0bec441fe8501ccf3c5560c1f01f3479926a2aa679`.

The core `fragmentStoresAndAtomics` contract is implemented and
hardware-qualified for every currently supported storage resource class.
The ICD does not advertise storage-image formats, while graphics descriptor
tables support storage buffers. The fragment probe performs both an atomic
increment and a uniquely indexed SSBO store for every covered pixel. Both
32/32 host suites, runner safety coverage, and the complete Prospero build
pass. The internal gate
(`20260728T131002Z-fragment-stores-atomics-run1.log`) and final public legacy
query/request gate (`20260728T131203Z-fragment-stores-atomics-run1.log`) each
reported exact `covered=18432 atomic=18432 stores=18432 marker=51a7c0de`,
matching SystemService self-exit, no stale process, and only the known single
`amount=0x4000` baseline warning. The public Prospero ELF SHA-256 is
`ecbd369db08ae5d7dd80fb66da45d282ef5134ca3d4c614940d1a86e5a2da985`.

The core `vertexPipelineStoresAndAtomics` contract is implemented and
hardware-qualified for the supported SSBO storage path. A combined
VS/TCS/TES/GS pipeline performs one atomic exchange and one direct store from
each applicable pre-fragment stage, then validates eight exact mapped-memory
markers and an exact 7,200-pixel green framebuffer. Supporting fused
TES-to-GS executables required the application-neutral tessellation recorder
to accept a primitive-stage vertex resource table and preserve it ahead of
descriptor-set tables. Both 33/33 normal and ASAN/UBSAN host suites, runner
safety coverage, and the complete Prospero build pass. Two final public legacy
query/request gates
(`20260728T133417Z-vertex-pipeline-stores-atomics-run1.log` and
`20260728T133515Z-vertex-pipeline-stores-atomics-run1.log`) each reported
exact `green=7200 stages=VS,TCS,TES,GS atomic=4 stores=4`, matching
SystemService self-exit, no stale process, and only the known single
`amount=0x4000` baseline warning. The public Prospero ELF SHA-256 is
`e79e33fe4bc5c8f780e1801456e3ea9bae4a1034148d873b039563ac11dd171a`.

The promoted Vulkan 1.1 `variablePointers` and
`variablePointersStorageBuffer` features are implemented and
hardware-qualified. Standalone and `VkPhysicalDeviceVulkan11Features` queries
report both bits, and device creation accepts both request paths. The bounded
SPIR-V 1.3 probe uses divergent OpSelect pointers for StorageBuffer reads,
StorageBuffer writes, and Workgroup writes/reads across 64 invocations, with
an exact 1,024-dword oracle and zero guards. The earlier corruption and GPU
faults were caused by dispatching Wave32 ACO code with the Wave64 packet
default, not by SPIR-V lowering. Vulkan now uses OpenAGC's explicit Wave32
compute modifier. Both 34/34 normal and ASAN/UBSAN suites pass; repeated public
FW 5.50 runs at `20260728T144957Z` and `20260728T145026Z` passed the exact
oracle, exited cleanly, and emitted only the known `amount=0x4000` baseline
warning. The public Prospero ELF SHA-256 is
`d6d0669f82d2fcd7bac06099eaee6aa9511c8620744548d0a952001779d2702f`.

## Runtime compatibility

| Area | Current evidence | State |
| --- | --- | --- |
| VMA | A configurable VMA consumer matches Eden's dynamic functions, external synchronization, upload/download/stream/device-local policies, images, manual bind, and block suballocation; direct and loader/VVL modes pass; one bounded FW 5.50 run passed every oracle and exited through SystemService with exact-PID removal | Hardware-qualified at this scope |
| Formats | Eden snapshots roughly 150 guest-relevant formats. The ICD exposes its existing uncompressed/depth subset plus all 14 BC1-BC7 UNORM/SNORM/SRGB/UFLOAT/SFLOAT formats for sampled, filtered, and transfer use. Multi-mip 2D/cube/cube-array images and nonzero mip views are host-qualified. D24, ASTC, and ETC remain fail-closed | Partial; broader uncompressed coverage and hardware BC sampling remain |
| Shader pipelines | VS/FS/CS/GS/tessellation, descriptors, specialization constants, push constants, vertex input, MRT, depth/stencil, and queries have qualified paths | Mandatory shader capabilities above remain incomplete |
| Indirect draws | Single/multi indexed and non-indexed commands record validated gfx1013 PM4 through OpenAGC; the complete two-draw gate validates BaseVertex, BaseInstance, InstanceIndex, and DrawID with exact equal-half readback | Hardware-qualified and publicly advertised; internal and public gates exited cleanly without a GPU reset |
| Buffer copies | `vkCmdCopyBuffer` records OpenAGC `DMA_DATA` per region after transfer-usage, binding, alignment, bounds, aliasing, address-range, and aggregate DCB-space validation; exact packet and rejection regressions pass | Host/Prospero qualified; deterministic FW 5.50 readback pending |
| Presentation | Standard headless surface plus FIFO swapchain is hardware-qualified for 1,800 frames | Eden PS5 surface hookup missing |

The BC slice is application-neutral Vulkan behavior, not an Eden override.
`vkGetPhysicalDeviceFormatProperties` reports the same sampled/filter/transfer
contract for all 14 formats, while image-format queries reject BC 3D images.
OpenAGC supplies exact block-aware layouts for every mip and layer. Vulkan
image views preserve complete allocation `MAX_MIP` metadata and program an
independent nonzero `BASE_LEVEL`/`LAST_LEVEL` interval. Normal and ASAN/UBSAN
suites pass 40/40, including VVL-clean five-mip BC7 cube views and exact linear
subresource layout checks; the full static/shared Prospero libraries build
without warnings. Hardware sampling and transfer qualification for this exact
candidate remains pending, so the audit does not claim it yet.

The first native command-form slice implements `vkCmdClearColorImage` for the
six 32-bit RGBA/BGRA UNORM/SRGB encodings. It expands remaining-mip/layer
ranges, queries every OpenAGC subresource layout, transitions and fills only
the selected byte intervals through public native commands, preserves SRGB
encoding and BGRA byte order, and rejects invalid layouts or depth images
without recording a partial usable command buffer. VVL covers a nonzero
two-mip/single-layer clear with a nontrivial color; normal and ASAN/UBSAN suites
pass 40/40 and Prospero static/shared libraries build clean. Eden's present
effects use RGBA16F, so this does not close their clear requirement: the next
meta-operation must support complete 1/2/4/8/16-byte color patterns rather
than special-casing Eden's current zero value.

The buffer-copy hardware candidate copies 64- and 80-byte regions at nonzero
source/destination offsets and verifies all 144 copied bytes plus 112 untouched
guard bytes. Prospero ELF SHA-256 is
`eac7fe30a1626502ae7ce27367ebeebdebc89fbf86a3a2b6738a15a6e09ab757`.
It remains pending deterministic FW 5.50 readback and should use the same
bounded, exact-PID lifecycle gate as the completed indirect-draw qualification.

## Implementation order

1. Preserve all four completed mandatory extension contracts. Driver and
   conservative float-control properties are query-complete, and mirror clamp
   plus vertex divisors are hardware-qualified. Float execution-mode
   capabilities remain conservatively false.
2. Promote mandatory feature groups only after command/compiler coverage and
   hardware qualification; prioritize the existing OpenAGC raster, blend,
   indirect-draw, query, and shader paths. Sampler-anisotropy descriptor
   semantics and the core feature contract are implemented, host-tested, and
   hardware-qualified by paired bilinear/16x filtering readback on FW 5.50.
   Typed OpenAGC topology/primitive-size state and exact readback have also
   qualified `largePoints`, static/dynamic `wideLines`, fragment demote with
   post-demote derivative participation, vertex-generated clip distance, and
   primitive cull distance, four-offset extended image gather, and fragment
   SSBO stores plus atomics.
   Indirect draw recording is no longer a stub. Compiler metadata includes
   DrawIndex, and DrawID-using multi draws expand into hardware-qualified single
   packets. The earlier submission fault was the zero-initialized non-indexed
   draw initiator; Vulkan now emits `DI_SRC_SEL_AUTO_INDEX=2` for non-indexed
   indirect and zero for indexed indirect. Later exact-color diagnostics exited
   cleanly and isolated the remaining combined BaseInstance/DrawID error to
   reversed openagc-psbc metadata: Mesa RADV's gfx10 order is BaseVertex,
   DrawID, BaseInstance. Compiler commit `d209d94` fixes and regression-tests
   that order. The internal and public FW 5.50 gates both produced exact
   `green=11472 left=5736 right=5736` readback, completed matching self-exit,
   and produced no PID-scoped fatal signal or GPU reset. Normal and sanitizer
   suites pass 26/26. `multiDrawIndirect`, `drawIndirectFirstInstance`, and
   `shaderDrawParameters` are now advertised and accepted through all relevant
   query and device-create paths. The rejected packet experiments, GPU-reset
   evidence, both root causes, and runner hardening are documented in
   `fw550_indirect_draw_parameters_20260728.md`.
3. Continue the general format matrix after the host-qualified 14-format BC
   slice: expand required uncompressed formats and run exact BC sampling/copy
   gates on both firmware endpoints. Keep D24 fail-closed until a correct
   fallback exists, and keep ASTC/ETC unsupported until conversion or native
   execution is implemented and qualified.
4. Complete native command forms in dependency order: arbitrary-format color
   clear, depth/stencil image and attachment clears, unscaled/scaled blits,
   then 4x resolves. The RGBA8 clear subset is complete; RGBA16F and the other
   advertised transfer-destination formats remain required before the command
   is generally complete.
5. Add only the allowed Eden changes: Prospero surface creation, build/link
   integration, and static Vulkan entrypoint location.
