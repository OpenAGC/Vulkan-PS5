# Eden Compatibility Profile

This is the live Milestone 6 gap matrix for upstream `../eden-ps5` revision
`612409c7ba`. The current `../eden-ps5` branch carries the PS5 integration
series through `a5e4fc4826`; the frozen format and command inventories below
still use the upstream revision so PS5-only glue cannot change the application
requirements. The Eden checkout was inspected read-only for this refresh; no
Eden file was changed.

Run the profile probe in reporting mode through CTest. `--strict` now succeeds:

```sh
ctest --test-dir build -R vulkan_ps5.eden_profile_report --output-on-failure
build/vulkan_ps5_eden_profile_test --strict
```

## Startup suitability

| Requirement | Eden requirement | Current evidence | State |
| --- | --- | --- | --- |
| API | Vulkan 1.1 or newer | ICD reports Vulkan 1.1 | Pass |
| Device extensions | `VK_EXT_vertex_attribute_divisor`, `VK_EXT_shader_demote_to_helper_invocation`, `VK_KHR_driver_properties`, `VK_KHR_sampler_mirror_clamp_to_edge`, `VK_KHR_shader_float_controls` | All five are enumerated, queryable, and accepted at device creation | Pass |
| Core/Features2 | 29 mandatory feature bits | All 29 are true; the geometry varying-export fix passes standalone and SDL/Zink hardware oracles | Pass |
| Limits | UBO range 65,536; 16 viewports; 8 color attachments; 8 clip distances | All four exact minima are reported | Pass |
| Queues | At least one graphics queue; present support when a surface exists | One universal graphics/compute/transfer queue is reported; the WSI family supports present | Pass |
| Swapchain | `VK_KHR_swapchain` when a surface is supplied | Enumerated and hardware-qualified in Milestone 4 | Pass |
| PS5 surface | A standard surface must be supplied by the PS5 frontend | `c01c1c5245` adds the `Ps5` window type, Vulkan-PS5 surface bridge, and static entrypoint lookup; later bootstrap commits compile and exercise it on FW 5.50 | Pass at bootstrap scope |

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

`multiViewport` is hardware-qualified through two static
viewport/scissor slots selected by `gl_ViewportIndex`. Both bounded FW 5.50
runs produced exactly 9,216 green and 9,216 red pixels, self-exited, and left
only the known `amount=0x4000` baseline warning. The corresponding host and
ASAN/UBSAN suites pass 39/39 with VVL-clean coverage. The later transparent-
black Zink conversion was traced to missing geometry-stage parameter-export
assignment in openagc-psbc. Compiler coverage, two exact SDL/Zink runs, and two
strengthened standalone GS-to-FS varying runs now pass; the ICD reports
`geometryShader = VK_TRUE` and the strict startup profile is zero-gap.

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
| Formats | Eden revision `612409c7ba` maps 112 guest `PixelFormat` entries to 109 unique Vulkan formats. The ICD directly maps 68 unique Vulkan image formats through OpenAGC API 52, including all 14 BC1-BC7 forms. Multi-mip 2D/cube/cube-array images and nonzero mip views are host-qualified; the 38-format scalar/vector gate passes 2,432 exact FW 5.50 clear/readback pixels twice. OpenAGC `c1ddcce` implements storage-width-compatible 8/16/32/64/128-bit color views and explicit BC-family pairs; Vulkan-PS5 `99e1249` validates and retains `VkImageFormatListCreateInfo`. ASTC/ETC2/EAC stay on Eden's transcode paths and D24 remains fail-closed | Direct inventory and earlier per-format gates are qualified; generalized mutable reinterpretation is implemented but still needs a clean integrated build and real-Eden FW 5.50/FW 11.60 qualification |
| Shader pipelines | VS/FS/CS/GS/tessellation, descriptors, specialization constants, push constants, vertex input, MRT, depth/stencil, and queries have qualified paths; fused-NGG geometry varyings pass standalone and SDL/Zink hardware oracles | Startup profile passes; real Eden shader-cache execution remains |
| Indirect draws | Single/multi indexed and non-indexed commands record validated gfx1013 PM4 through OpenAGC; the complete two-draw gate validates BaseVertex, BaseInstance, InstanceIndex, and DrawID with exact equal-half readback | Hardware-qualified and publicly advertised; internal and public gates exited cleanly without a GPU reset |
| Buffer copies | `vkCmdCopyBuffer` records OpenAGC `DMA_DATA` per region after transfer-usage, binding, alignment, bounds, aliasing, address-range, and aggregate DCB-space validation; exact packet/rejection regressions and two deterministic FW 5.50 readback runs pass | FW 5.50 hardware-qualified at this scope |
| Presentation | Standard headless surface plus FIFO swapchain is hardware-qualified for 1,800 frames; Eden's PS5 surface bridge and 600-frame bootstrap are FW 5.50-qualified | Real Eden renderer/presentation workload remains |

The revision-frozen source inventory is committed as
`analysis/eden-format-inventory-612409c7ba.tsv`. It is derived from Eden's
`SURFACE_FORMAT_LIST`, preserving all 112 guest rows, their requested usage,
the 109 unique Vulkan mappings, and the current direct/transcode/fail-closed
classification. This replaces the earlier approximate “roughly 150” count.
ASTC and ETC2/EAC remain unadvertised in the ICD so Eden selects its existing
RGBA8/BC or R/RG transcode paths. `VK_FORMAT_X8_D24_UNORM_PACK32` and
`VK_FORMAT_D24_UNORM_S8_UINT` remain unadvertised and image creation rejects
them.

The command-use inventory is likewise frozen from `612409c7ba`, not inferred
from the PS5 bootstrap. Eden's Vulkan wrapper loads and the renderer calls
buffer copy/fill, buffer-image and image-buffer copies, image copies, color
image clears, attachment clears, color blits, and color resolves. Concrete
renderer call sites include present-effect `ClearColorImage`, rasterizer and
window-adapt `ClearAttachments`, texture-cache/present `BlitImage`, MSAA
`ResolveImage`, and the buffer/texture/query-cache transfer families. The base
revision does not load or directly call `vkCmdClearDepthStencilImage`; its
depth/stencil clear and conversion helpers use render-pass graphics paths and
attachment clears. Vulkan-PS5 still implements the general image command, but
it is not evidence for an Eden requirement. All directly inventoried command
families now record public OpenAGC work or fail closed for the documented
unsupported subforms. The startup profile is zero-gap; the next evidence gap is
real Eden shader-cache and renderer execution, not a silent command stub.

### Mutable image-view compatibility audit

This audit is frozen to Eden `612409c7ba`, even though the observed PS5
diagnostic was produced by the integration branch. Eden's
`src/video_core/compatible_formats.cpp` defines its image-view relationships,
and `src/video_core/renderer_vulkan/vk_texture_cache.cpp` converts those
relationships into a `VkImageFormatListCreateInfo`. When a format has more
than one view, Eden sets both `VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT` and
`VK_IMAGE_CREATE_EXTENDED_USAGE_BIT`; when `VK_KHR_image_format_list` is
available it supplies the complete list at image creation. This is normal
Vulkan behavior and must not be reduced to a swapchain-only RGBA8 exception.

The pinned Eden requirements and the current native boundary are:

| Compatibility family | Eden `612409c7ba` requirement | Current implementation | Qualification state |
| --- | --- | --- | --- |
| 8-bit uncompressed | R8 UINT/UNORM/SINT/SNORM are mutually view-compatible | Vulkan-PS5 groups every implemented native 8-bit color format; OpenAGC `c1ddcce` accepts single-plane, non-depth 1x1 formats with the same one-byte texel width | Source and OpenAGC unit coverage present; no mutable-view firmware oracle |
| 16-bit uncompressed | R16 and RG8 float/integer/normalized members share one view class | Vulkan-PS5 groups the implemented packed-16, RG8, and R16 formats; OpenAGC applies the same two-byte storage rule | Source and representative OpenAGC unit coverage present; no mutable-view firmware oracle |
| 32-bit uncompressed | Eden's native-BGR class contains 19 formats, including `B8G8R8A8_UNORM` and `A8B8G8R8_UNORM_PACK32` | Vulkan-PS5 enumerates the implemented 32-bit members, validates every non-empty image format-list entry, retains the exact list on the image, and rejects a compatible format omitted from that list. OpenAGC applies its four-byte storage rule | OpenAGC's 19-test/19,926-assertion host gate, Vulkan-PS5's 62-test host gate, both Prospero builds, and the exact FW 5.50 mutable-view attachment clear pass. Other 32-bit reinterpretations and FW 11.60 remain unqualified |
| 64-bit uncompressed | RG32 and RGBA16 float/integer/normalized members share one view class | Vulkan-PS5 groups every native member; OpenAGC applies its eight-byte storage rule | Source and representative OpenAGC unit coverage present; no mutable-view firmware oracle |
| 128-bit uncompressed | RGBA32 float/uint/sint share one view class | Vulkan-PS5 groups all three native members; OpenAGC applies its sixteen-byte storage rule | Source and representative OpenAGC unit coverage present; no mutable-view firmware oracle |
| 96-bit uncompressed | Eden lists only `R32G32B32_FLOAT`, so the view table adds no non-identity relationship | Vulkan-PS5 deliberately has no 96-bit mutable class and does not expose RGB32 images | Correctly remains buffer-only/fail-closed for images |
| BC compressed | Eden requires UNORM/SNORM or UNORM/SRGB pairing for BC4, BC5, BC6H, and BC7; BC1/BC2/BC3 are identity-only in the pinned Eden view table | Vulkan-PS5 implements separate BC1 through BC7 families. OpenAGC `c1ddcce` uses explicit family IDs, so equal block byte size alone cannot make BC1 compatible with BC4 | Existing BC sampling/copy execution is qualified on FW 5.50, but mutable BC reinterpretation itself is not; FW 11.60 replay is pending |
| ASTC | Eden defines UNORM/SRGB pairs for each of 14 block dimensions, but the inventory classifies all 28 Vulkan mappings as `transcode_required_astc` | Neither Vulkan-PS5 nor OpenAGC admits ASTC as a native mutable format. The ICD must remain unadvertised so Eden selects its RGBA8/BC transcode path | Fail closed at the native boundary; the real-Eden transcode path remains unqualified |
| ETC2/EAC | The inventory contains ten formats classified `transcode_required_etc2_eac`; Eden does not add cross-format mutable pairs for them | No native Vulkan-PS5/OpenAGC image format or mutable-view promise | Fail closed at the native boundary; the real-Eden transcode path remains unqualified |
| D24 | Three guest rows map to the two Vulkan D24 spellings and require attachment use | Vulkan-PS5 does not advertise or create either D24 image format. OpenAGC's compatibility predicate explicitly rejects depth/stencil formats | Intentional fail-closed policy until a correct native path or conversion exists |

OpenAGC commit `c1ddcce84fd77bf262d34a494b92fdb4936a1168`
keeps Vulkan policy out of the public native runtime. It derives ordinary
color compatibility from `AgcRuntimeFormatInfo` only when both formats are
single-plane, non-depth/stencil, one-texel blocks with equal byte widths. BC
formats instead use explicit BC1/BC2/BC3/BC4/BC5/BC6/BC7 family identifiers;
the regression rejects the tempting but invalid equal-size BC1-to-BC4 case.
The OpenAGC host gate passes after this change. The Vulkan layer remains
responsible for Vulkan class membership, mutable-create flags, and exact
format-list retention/enforcement.

At the Vulkan layer, a present but empty `VkImageFormatListCreateInfo` is not
a restriction. A non-empty list is copied into the `VkPs5Image` object after
every entry is proven native and compatible. `vkCreateImageView` then requires
both general mutable-class compatibility and membership in that retained list.
Attachment-clear meta-pipeline selection uses the framebuffer image view's
format, not the allocation's base format. The targeted regression constructs
a `VK_FORMAT_B8G8R8A8_UNORM` mutable image, creates and clears an
`VK_FORMAT_A8B8G8R8_UNORM_PACK32` view from its declared 19-member Eden list,
and requires an otherwise compatible omitted format to fail.

These implementation facts promote only the exact traced mutable-view clear
path to FW 5.50 hardware-qualified. Cleanup-first run
`examples/qualification-logs/20260802T115623Z-swapchain-run1.log`, using Eden
ELF SHA-256
`a707e44eb900c399af594adaed45f7b28a20363dab53bade741b9f61edca64d8`,
creates the format-51 view over the format-44 image, binds the view-compatible
clear pipeline, and records a native draw. It then stops at the later sampled-
read to storage-write image-transition contract, so it does not prove
presentation or orderly teardown. Closure still requires two cleanup-first
real-Eden FW 5.50 runs proving sustained render/presentation, bounded teardown,
exact process absence, and immediate relaunch. The identical pinned bytes must
then pass on FW 11.60. Other uncompressed reinterpretations, mutable BC, ASTC,
ETC2/EAC, and D24 execution must not be inferred from the exact traced result.

The factual sources for this section are the pinned Eden files
`src/video_core/compatible_formats.cpp` and
`src/video_core/renderer_vulkan/vk_texture_cache.cpp`,
`analysis/eden-format-inventory-612409c7ba.tsv`, OpenAGC `src/runtime.c` and
`tests/test_runtime.c` at `c1ddcce`, and Vulkan-PS5
`src/vulkan_ps5_core.c` plus `tests/command_recording.c` at `99e1249`.

The first refreshed uncompressed slice adds the four directly native integer
targets `R16G16B16A16_{UINT,SINT}` and `R32G32B32A32_{UINT,SINT}`. They expose
sampled, storage, color-attachment, and transfer features, but deliberately do
not expose linear filtering or blit features. OpenAGC translates their public
format values to exact gfx1013 SQ resource encodings; Vulkan creates optimal
images and native views, preserves signed/unsigned clear bits, and keeps texel-
buffer features false because typed buffer-view descriptors are not yet
implemented. The identical bounded probe ELF created all four native image
views and passed two back-to-back FW 5.50 runs with 256 exact signed/unsigned
clear/readback pixels, clean teardown, and immediate relaunch; see
`analysis/fw550_integer_formats_20260801.md`. Storage-image execution is now
qualified by the 30-format gate below. Sampled-image and integer attachment
exports plus FW 11.60 replay remain before the format profile is endpoint-
qualified.

The second refreshed uncompressed slice consumes OpenAGC API 48 for fourteen
formats already represented by its qualified gfx1013 target table: R/RG
16-bit UNORM, SNORM, UINT, and SINT; RGBA16 UNORM and SNORM; and R/RG 32-bit
UINT and SINT. Normalized formats expose filter and blit support while integer
forms do not; both expose sampled, storage, color-attachment, and transfer
use, with texel-buffer features kept false. Exact layout, sampled-view,
component-class, scalar/vector export, image/view creation, format-query, and
clear-packing tests pass. The full normal and sanitizer suites pass 51/51 and
the complete Prospero build is clean. An expanded identical ELF then passed
all eighteen new formats and 1,152 exact clear/readback pixels twice on FW
5.500.008 with bounded waits and immediate relaunch; see
`analysis/fw550_scalar_vector_formats_20260801.md`. Storage-image execution is
now qualified by the 30-format gate below; sampled-image execution, attachment
exports, and the FW 11.60 endpoint replay remain pending.

The third refreshed uncompressed slice consumes OpenAGC API 49 for R8/RG8
SNORM, UINT, and SINT, and corrects R8/RG8 UNORM storage-image creation. The
six new formats have exact gfx1013 sampled descriptors and color-target number
classes; normalized formats filter and blit while integer formats do not.
Together with the two UNORM forms, they create sampled/storage/attachment/
transfer images and native views. Both 55/55 host suites and the complete
Prospero build pass. Pinned ELF
`73783127fb59f8a31e6c5cdc7500d5d45da5b78ce3d694cd767d92dd72b9f3ed`
then passed 26 formats and all 1,664 exact pixels twice on FW 5.500.008 with
cleanup-first execution, a two-second fence, teardown, PID absence, and
immediate relaunch. See `analysis/fw550_r8_rg8_formats_20260801.md`.

The fourth refreshed uncompressed slice consumes OpenAGC API 50 for packed
RGBA8 SNORM/UINT/SINT, RGB10A2 UINT, and BGR10A2 UNORM. Vulkan preserves the
exact component order and numeric class for Eden's `A8B8G8R8`,
`A2B10G10R10`, and `A2R10G10B10` mappings. The BGR UNORM form is sampled,
filterable, transferable, blit-capable, and attachable without a false
storage-image claim; the other four expose storage as appropriate. Both 55/55
host suites and the complete Prospero build pass. Pinned ELF
`07384ba86e1db7b69b3994be320fe4a35fc05db6eec1773d761aa2a9a66602b8`
then passed all 31 formats and 1,984 exact pixels twice on FW 5.500.008. See
`analysis/fw550_packed_formats_20260801.md`.

The fifth refreshed uncompressed slice consumes OpenAGC API 51 for R5G6B5,
B5G6R5, R5G5B5A1, A1R5G5B5, A4B4G4R4, and R4G4 UNORM. The five 16-bit forms
are sampled, filterable, transferable, blit-capable, and attachable without
false storage or texel-buffer claims. R4G4 is sampled/filterable and
transferable with source blit only. Both 55/55 host suites and the complete
Prospero build pass. Pinned ELF
`0a5b5f4a89d2a2b52dd54e935d8c7385215197b22fdbad63dd0fd5287b12f07d`
then passed all 37 formats and 2,368 exact pixels twice on FW 5.500.008. See
`analysis/fw550_packed16_formats_20260801.md`.

The sixth refreshed uncompressed slice consumes OpenAGC API 52 for RGB9E5
shared-exponent images. `VK_FORMAT_E5B9G9R9_UFLOAT_PACK32` is sampled,
filterable, transferable, and source-blit capable without false attachment,
storage, or texel-buffer claims. Clean normal and ASAN/UBSAN suites pass
57/57, the complete Prospero build passes, and pinned ELF
`29a2cd389a895e29275a3c527a6668c217dc53cd897748f02625ad3dc34b60d3`
passed all 38 formats and 2,432 exact pixels twice on FW 5.500.008. The only
remaining inventory entry formerly classified as an uncompressed image gap,
`VK_FORMAT_R32G32B32_SFLOAT`, is now explicitly classified buffer-only because
the gfx10.3 format table rejects it for images. See
`analysis/fw550_rgb9e5_format_20260801.md`.

The refreshed formats now also have real storage-image shader execution rather
than capability-query and clear-only evidence. Three formatless compute
pipelines write the float, unsigned, and signed classes across all 30 formats
that advertise storage support. Every target has its own descriptor set and
receives a reflected 16-byte push constant; exact linear-layout readback
checks 480 pixels. The first FW 5.50 run exposed the driver's incorrect SNORM
-1 clear packing. Vulkan reserves -128/-32768 during float-to-SNORM conversion,
so the packer now emits -127/-32767 and the corrected 38-format clear oracle
also passes twice. Pinned storage ELF
`8b15a1053c9f7bdfb57f419f10f0b761563009a8d0056bb3c07d8b9e24d379b2`
passed twice on FW 5.500.008 with bounded waits, teardown, and immediate
relaunch. See `analysis/fw550_format_storage_20260802.md`. Sampled-image
execution and scalar/vector attachment exports remain pending.

The Flappy startup format audit exposed a capability-table gap for ten formats
whose native OpenAGC image and storage-descriptor mappings already exist:
`B8G8R8A8_UNORM`, `A8B8G8R8_UNORM_PACK32`, `A2B10G10R10_UNORM_PACK32`,
`R16_SFLOAT`, `R16G16_SFLOAT`, `R16G16B16A16_SFLOAT`, `R32_SFLOAT`,
`R32G32_SFLOAT`, `R32G32B32A32_SFLOAT`, and
`B10G11R11_UFLOAT_PACK32`. Vulkan-PS5 now advertises storage-image support for
those formats and accepts Eden's combined sampled/storage/attachment/transfer
usage without relying on `VK_IMAGE_CREATE_EXTENDED_USAGE_BIT`. The lifecycle
test checks the exact feature set and image-format query for every format; the
command-recording test retains an sRGB storage-negative case. All 62 host tests
pass. Hardware storage writes for this expanded set remain a qualification
gate; this change does not broaden combined D32/S8 sampling, whose prior FW
5.50 probe left the native queue pending.

The BC slice is application-neutral Vulkan behavior, not an Eden override.
`vkGetPhysicalDeviceFormatProperties` reports the same sampled/filter/transfer
contract for all 14 formats, while image-format queries reject BC 3D images.
OpenAGC supplies exact block-aware layouts for every mip and layer. Vulkan
image views preserve complete allocation `MAX_MIP` metadata and program an
independent nonzero `BASE_LEVEL`/`LAST_LEVEL` interval. Normal and ASAN/UBSAN
suites pass 40/40, including VVL-clean five-mip BC7 cube views and exact linear
subresource layout checks; the full static/shared Prospero libraries build
without warnings. A deterministic compute probe now samples all fourteen
formats through native Vulkan sampler and combined-image-sampler descriptors.
After its smoke stage exposed and drove a firmware-neutral OpenAGC BC5
absent-channel fix, one pinned ELF passed the complete format set twice on FW
5.500.008 with bounded waits, teardown, and immediate relaunch; see
`analysis/fw550_bc_sampling_20260801.md`. Direct-upload linear sampling is
therefore qualified on FW 5.50. A second pinned ELF copies source mip 0/1 into
destination mip 1/2 for all fourteen formats, verifies all 28 regions and an
untouched destination mip, and passes twice under the same lifecycle gate; see
`analysis/fw550_bc_copy_20260801.md`. Final FW 11.60 replay remains pending.

The native `vkCmdClearColorImage` path now covers every advertised
uncompressed color format, including the RGBA16F present-effect format. It
packs arbitrary 1/2/4/8/16-byte UNORM, SRGB, half, float, RGB10A2, and
R11G11B10 patterns through a committed meta compute shader and public OpenAGC
pipeline, descriptor, push-constant, and dispatch APIs. Every selected mip and
layer uses its queried native byte layout, while regular array layers batch
into one two-dimensional dispatch per mip. OpenAGC resource-table snapshots
preserve previous dispatch state, allocation-free inline updates avoid a
per-dispatch arena limit, and application graphics/compute state is rebound
after the meta operation. Normal and ASAN/UBSAN suites pass 41/41,
including VVL RGBA16F ranges, generated-shader reproducibility, allocation
failure cleanup, and compressed/depth fail-closed cases; Prospero static/shared
libraries build clean. This closes the host clear requirement generally, not
only Eden's current zero clear. Exact hardware pixel execution is still
pending.

`vkCmdClearDepthStencilImage` now covers the advertised D16, D32, S8,
D16+S8, and D32+S8 single-sample formats without an Eden-specific path. Each
selected plane, mip, and layer interval uses its queried native layout, and
regular array layers batch into one compute dispatch per plane and mip. The
host gate uses a 70-layer combined D32+S8 image and records exactly two
dispatches for one selected mip. Clean normal and ASAN/UBSAN suites pass 56/56
and the Prospero libraries build clean. One pinned ELF passed twice on FW
5.500.008 with exact D16, D32, S8, D16+S8, and D32+S8 readback (`depth=256
stencil=192`), bounded synchronization, teardown, and immediate relaunch.
D24 stays unadvertised; multisampled and partial attachment clears remain
fail-closed pending their general Vulkan implementations, and the identical
FW 11.60 replay remains pending.

Attachment clears are now application-neutral Vulkan operations, not an Eden
override. `vkCmdClearAttachments` and both render-pass and dynamic-rendering
load clears use reproducible graphics-meta shaders and lazily cached native
pipelines for partial color, depth, stencil, and combined depth/stencil
rectangles. Normal and sanitizer suites pass 48/48; the FW 5.50 color/load-
clear oracle passed twice with exact `green=1152 clear=2944`, teardown, and
immediate relaunch on both FW 5.500.008 and FW 11.600.005 using one pinned ELF.
Depth/stencil hardware pixels remain pending, so cross-firmware qualification
currently applies only to color attachment/load clears.

General color blits are also application-neutral. The ICD uses a committed
graphics-meta shader with public OpenAGC pipelines, descriptors, views,
samplers, transitions, and draws for nearest/linear scaling, reversed axes,
mips, and array layers. BC and uncompressed color sources are accepted;
destinations use the supported uncompressed color-target matrix. The exact
nearest 2x readback probe passed twice on FW 5.500.008 with 256 copied pixels
and 144 guards, then the same ELF passed twice on FW 11.600.005 with an exact
FTP round-trip hash. Public OpenAGC 3D sampled views and depth-slice color
bindings now also back 3D color blits, while disjoint-subresource self blits
use exact recorded subresource state and symmetric restoration. Their combined
FW 5.50 gate passed `3d=512 self=64 guard=160` with clean exit. Same-
subresource feedback blits, depth/stencil blits, and compressed destinations
stay fail-closed.

Depth-only dynamic-rendering pipelines no longer require a fictitious color
attachment. A zero-color `VkPipelineRenderingCreateInfo` and zero-attachment
color-blend state now compile a depth-exporting fragment shader into a native
OpenAGC D32 graphics pipeline. This is a general Vulkan correction for depth
passes and the prerequisite for graphics-meta depth/stencil attachment clears.

The buffer-copy hardware gate copies 64- and 80-byte regions at nonzero
source/destination offsets and verifies all 144 copied bytes plus 112 untouched
guard bytes. Pinned Prospero ELF
`8429fb631a76db85b5f2f54e99952c1eafd96670672758c3eb1ba4790789b8e8`
passed twice on FW 5.500.008 through the bounded cleanup-first, exact-PID
lifecycle runner with immediate relaunch and no target-attributed panic,
reset, timeout, or GPU fault. See `analysis/fw550_buffer_copy_20260802.md`.

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
   The geometry startup requirement is closed: openagc-psbc assigns GS
   parameter exports, Vulkan advertises the feature, and both standalone and
   SDL/Zink application-level hardware oracles pass. The next gate is real Eden
   shader-cache and renderer execution rather than another startup bit.
3. Continue the general format matrix after the FW 5.50-qualified 14-format BC
   sampling and cross-mip copy slices: expand required uncompressed formats,
   then replay the final identical candidate on both firmware endpoints. Keep
   D24 fail-closed until a correct
   fallback exists, and keep ASTC/ETC unsupported until conversion or native
   execution is implemented and qualified.
4. Complete the remaining native command forms in dependency order. General
   2D/3D and disjoint-subresource self color blits plus 4x-to-1x 2D
   color resolves are host-complete and exact-pixel qualified on both
   endpoints except that the 3D/self combined gate currently has FW 5.50
   evidence only. Partial attachment clears are
   implemented; their color/load-clear path is identically qualified on FW
   5.50 and FW 11.60, while depth/stencil pixel gates remain. General
   uncompressed color and single-sample depth/stencil image clears are
   host-complete and await their hardware pixel oracles. Preserve same-
   subresource feedback blits, depth/stencil blits, compressed destinations,
   and unsupported resolve/sample-count forms as explicit gaps until Eden
   traces prove they are needed and a general native path exists.
5. Preserve the completed allowed Eden surface/static-entrypoint changes and
   expand the bootstrap into the real renderer incrementally. Capture actual
   command and format use before enabling any additional Vulkan capability;
   keep RmlUi, input, audio, shader cache, game boot, and teardown ownership in
   Eden rather than adding frontend policy to the ICD.
