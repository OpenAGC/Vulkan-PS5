# Eden Compatibility Profile

This is the live Milestone 6 gap matrix for `../eden-ps5` revision
`39763e7321`. The Eden checkout was inspected read-only; its existing deleted
`AGENTS.md`/`CLAUDE.md` and untracked `.serena/` state were not changed.

Run the profile probe in reporting mode through CTest. Use `--strict` when a
nonzero exit is desired until every hard startup requirement passes:

```sh
ctest --test-dir build -R vulkan_ps5.eden_profile_report --output-on-failure
build/vulkan_ps5_eden_profile_test --strict
```

## Startup suitability

| Requirement | Eden requirement | Current evidence | State |
| --- | --- | --- | --- |
| API | Vulkan 1.1 or newer | ICD reports Vulkan 1.1 | Pass |
| Device extensions | `VK_EXT_vertex_attribute_divisor`, `VK_KHR_driver_properties`, `VK_KHR_sampler_mirror_clamp_to_edge`, `VK_KHR_shader_float_controls` | Driver properties and conservative float-control properties are enumerated, queryable, and accepted at device creation | 2 gaps |
| Core/Features2 | 29 mandatory feature bits | `geometryShader`, `tessellationShader`, and `hostQueryReset` are true | 26 gaps |
| Limits | UBO range 65,536; 16 viewports; 8 color attachments; 8 clip distances | All four exact minima are reported | Pass |
| Queues | At least one graphics queue; present support when a surface exists | One universal graphics/compute/transfer queue is reported; the WSI family supports present | Pass |
| Swapchain | `VK_KHR_swapchain` when a surface is supplied | Enumerated and hardware-qualified in Milestone 4 | Pass |
| PS5 surface | A standard surface must be supplied by the PS5 frontend | Eden currently has no Prospero surface/build integration and treats headless as no surface | Gap outside ICD |

The sampler mirror-clamp contract has a hardware candidate but is not counted
as supported yet. `vulkan_ps5_mirror_clamp_probe` samples `(-0.5, -0.5)` and
requires the mirror-once gray texel instead of the red edge-clamp texel. The
one-shot runner has host-tested clean and crash paths, exact-PID cleanup, and
bounded warning handling. The Prospero candidate SHA-256 is
`8ffe2a48c074391e0e96c56d03699a9f887b21ae0b15721be2d89a0cf24fe5da`.
One fresh-console FW 5.50 run is still required before enumeration.

The vertex-divisor software contract is also implemented but not yet
advertised. openagc-psbc API v8 passes input rate and divisor state into RADV's
gfx1013 lowering, and Vulkan pipeline creation consumes
`VkPipelineVertexInputDivisorStateCreateInfoEXT` with divisor-one defaults and
nonzero-divisor validation. Compiler and pipeline regressions pass on host and
both components build for Prospero. Legacy EXT and promoted properties report
the compiler's `UINT32_MAX` nonzero range; Features2 returns divisor and
zero-divisor support false, and device creation rejects either unsupported
request. A deterministic hardware readback probe followed by feature promotion
and extension enumeration is still required.
The probe and one-shot runner are now prepared: four overlapping instances
must resolve to an exact-white triangle only when divisor two is honored. Its
runner safety regression and both full 24-test host suites pass. Prospero
candidate SHA-256 is
`7105fbd6960cf97ac12e7e66bed5a34d71310a715aaea615bd8210d3aaea8c49`;
one fresh-console FW 5.50 run remains before public promotion.

The automated probe currently reports:

```text
eden-profile: extensions=2 features=23 limits=0 queues=0 total=25
```

The 23 feature gaps are `depthBiasClamp`,
`drawIndirectFirstInstance`, `dualSrcBlend`,
`fragmentStoresAndAtomics`, `imageCubeArray`, `independentBlend`, `largePoints`,
`logicOp`, `multiDrawIndirect`, `multiViewport`, `robustBufferAccess`,
`samplerAnisotropy`, `sampleRateShading`,
`shaderClipDistance`, `shaderCullDistance`, `shaderImageGatherExtended`,
`shaderStorageImageWriteWithoutFormat`, `vertexPipelineStoresAndAtomics`,
`wideLines`, `shaderDemoteToHelperInvocation`, `shaderDrawParameters`,
`variablePointers`, and `variablePointersStorageBuffer`.

These bits must only be enabled as their complete Vulkan and shader semantics
become hardware-qualified. Eden continuing after logging an unsuitable driver
does not turn a failed requirement into support.

The host-side `independentBlend` contract is now implemented. Graphics
pipelines translate distinct per-attachment enable state, non-dual-source
factors, all five core blend operations, separate alpha equations, write masks,
and constants into OpenAGC's typed gfx1013 blend state. Every baseline,
geometry, indirect, and tessellation draw restores that state. The exact PM4
regression uses one disabled full-mask target and one enabled GB-only target,
requiring distinct control words, target mask `0x6f`, and all four constants.
Both normal and ASAN/UBSAN suites pass 24/24 and the Prospero build passes.
The standalone MRT gate now requires target zero to retain opaque green while
target one resolves to half-intensity magenta through constant-color/alpha
factors, followed by the shared SystemService lifecycle. Since exact 0.5 is a
tie when converted to 8-bit UNORM, the gate accepts only `0x7f7f007f` or
`0x80800080`.
Its runner covers clean, fatal, NUL-containing, PID-reuse, and exact-identity
paths. Both host configurations pass 24/24; Prospero ELF SHA-256 is
`8ed187f8781a34717481d6f3c186f5ebe645d76e48989fc985503a9878c18da7`.
The first bounded hardware run produced the correct target-zero coverage and
nonzero target-one coverage, then completed the matching self-kill lifecycle
without a fatal GPU signature. Its obsolete single-value oracle rejected the
target-one pixels without logging their value. The next candidate logs both
center pixels and accepts only the two legal half-intensity encodings. Public
`independentBlend` therefore remains false; dual-source blend remains a
separate unsupported feature.

The host-side `logicOp` contract is now complete without advertising the
feature. Every one of the 16 core `VkLogicOp` values maps to OpenAGC's typed
gfx1013 ROP3 truth table. Active logic state suppresses per-target blending,
while disabled state restores COPY; baseline, indexed, indirect, geometry, and
tessellation draw paths all restore the pipeline state. Pipeline rejection and
exact `CB_COLOR_CONTROL=0x00660010` XOR regressions pass in both 24/24 host
configurations. The bounded probe fills mapped RGBA8 destination pixels with
`0x55aa33cc`, draws green through XOR, and requires exact `0xaaaacccc` triangle
coverage plus unchanged background before the shared matching-self-kill exit.
Its Prospero ELF links the required runtime set and has SHA-256
`a60b0210dbe5836ffb0ed30082c6fc33ddfc0ff80cab9d9a60e172c7746d83b7`.
Public `logicOp` remains false pending one bounded FW 5.50 run.

The host-side `depthBiasClamp` contract is now implemented without advertising
the feature. Static pipelines and `VK_DYNAMIC_STATE_DEPTH_BIAS` both preserve
constant, clamp, and slope factors; `vkCmdSetDepthBias` records command-local
dynamic values. Baseline, indexed, indirect, geometry, and tessellation paths
enable front/back polygon offset and emit OpenAGC's typed D16/D32 format,
clamp, slope, and constant registers. Exact PM4 and pipeline regressions pass
in both 24/24 host configurations. The standalone bounded probe supplies an
oversized constant bias with clamp 0.125 and requires raw D32 depth to move
from 0.25/0.75 to exact 0.375/0.875 while preserving the established color and
stencil decisions. Its runner covers matching-self-kill, NUL, later-PID, fatal,
and exact-identity paths. The Prospero ELF links the required runtime set and
has SHA-256
`8e73f6cd45ad1b3ba9f21fc8b1956582190b23a2422d65c0827875506c89aa57`.
Public `depthBiasClamp` remains false pending one bounded FW 5.50 run.

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
paths accept a normal request. `wideLines` and `largePoints` remain separate,
unsupported capabilities. The rebuilt public-path probe queried and requested
the advertised feature, reproduced the exact 230/3 oracle and clean lifecycle,
and has Prospero ELF SHA-256
`fd2dd48dddd46cd2519bd06fb5b9dacb6bc394658a1efc9d626b2258c9cdeeb3`.

## Runtime compatibility

| Area | Current evidence | State |
| --- | --- | --- |
| VMA | A configurable VMA consumer matches Eden's dynamic functions, external synchronization, upload/download/stream/device-local policies, images, manual bind, and block suballocation; direct and loader/VVL modes pass; one bounded FW 5.50 run passed every oracle and exited through SystemService with exact-PID removal | Hardware-qualified at this scope |
| Formats | Eden snapshots roughly 150 guest-relevant formats. The ICD currently exposes 19 uncompressed/depth formats, no BC formats, no D24, and no storage-image feature bits | Major gap |
| Shader pipelines | VS/FS/CS/GS/tessellation, descriptors, specialization constants, push constants, vertex input, MRT, depth/stencil, and queries have qualified paths | Mandatory shader capabilities above remain incomplete |
| Indirect draws | Single/multi indexed and non-indexed commands record validated gfx1013 PM4 through OpenAGC; corrected one-draw BaseVertex/BaseInstance readback passed with clean SystemService exit and no GPU reset | Hardware-qualified at one-draw parameter scope; complete multi/DrawID gate pending before public feature promotion |
| Buffer copies | `vkCmdCopyBuffer` records OpenAGC `DMA_DATA` per region after transfer-usage, binding, alignment, bounds, aliasing, address-range, and aggregate DCB-space validation; exact packet and rejection regressions pass | Host/Prospero qualified; deterministic FW 5.50 readback pending |
| Presentation | Standard headless surface plus FIFO swapchain is hardware-qualified for 1,800 frames | Eden PS5 surface hookup missing |

The buffer-copy hardware candidate copies 64- and 80-byte regions at nonzero
source/destination offsets and verifies all 144 copied bytes plus 112 untouched
guard bytes. Prospero ELF SHA-256 is
`eac7fe30a1626502ae7ce27367ebeebdebc89fbf86a3a2b6738a15a6e09ab757`.
It must not be launched while PS5 interaction is suspended following the
uncaptured console crash reported after the indirect-parameter deployment
timeout.

## Implementation order

1. Complete the two remaining mandatory extension contracts: sampler mirror
   clamp and vertex divisors with real sampler/pipeline semantics. The
   query-only driver/float-control contracts are complete and tested; float
   execution-mode capabilities remain conservatively false. The mirror-clamp
   hardware probe and bounded runner are prepared; public enumeration awaits
   its single FW 5.50 qualification run. Vertex-divisor compiler and pipeline
   semantics and bounded readback gate are prepared; the one hardware run and
   public query contract remain.
2. Promote mandatory feature groups only after command/compiler coverage and
   hardware qualification; prioritize the existing OpenAGC raster, blend,
   indirect-draw, query, and shader paths. Sampler-anisotropy descriptor
   semantics are implemented and host-tested. Its deterministic paired
   bilinear/16x filtering probe, mapped-memory contrast oracle, bounded runner,
   and Prospero ELF are prepared; one fresh-console run remains before normal
   feature advertisement and device-enable handling can be promoted. Indirect
   draw recording is no longer a stub. Compiler metadata now includes DrawIndex,
   and DrawID-using multi draws expand into hardware-qualified single packets.
   Its paired multi-draw/nonzero-first-instance/DrawID exact-color gate faulted
   during submission even after reverting to sequential single packets. A
   one-draw BaseVertex/BaseInstance diagnostic reproduced the exact fault and
   proved DrawID was not required. Comparison with upstream RADV and OpenAGC's
   passing fixture identified a zero-initialized non-indexed draw initiator:
   Vulkan now emits `DI_SRC_SEL_AUTO_INDEX=2` for non-indexed indirect and zero
   for indexed indirect, with exact host packet assertions. The corrected
   one-draw candidate produced the exact expected GPU readback without a reset,
   then faulted only after PASS while returning through the raw-ELF exit path.
   The rebuilt probe uses SystemService self-kill after Vulkan cleanup, and all
   bounded runners sanitize NUL bytes before parsing klog lifecycle evidence.
   The clean lifecycle rerun passed with the exact oracle, matching self-kill,
   idle queues, and no fatal signal or GPU reset. The complete multi-draw/DrawID
   candidate now has packet-boundary-safe normal and sanitizer tests for the
   exact BaseVertex/BaseInstance locations and DrawIndex `0,0,1,0,1` sequence,
   plus the shared SystemService lifecycle gate. Its one bounded hardware run
   remains before the three related core bits can be promoted. The rejected
   packet experiments, GPU-reset evidence, root cause, and runner hardening are documented in
   `fw550_indirect_draw_parameters_20260728.md`.
3. Expand qualified uncompressed and BC format support, with D24 fallback kept
   honest and ASTC/ETC remaining unsupported until conversion is implemented.
4. Add only the allowed Eden changes: Prospero surface creation, build/link
   integration, and static Vulkan entrypoint location.
