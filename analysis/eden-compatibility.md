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
| Device extensions | `VK_EXT_vertex_attribute_divisor`, `VK_KHR_driver_properties`, `VK_KHR_sampler_mirror_clamp_to_edge`, `VK_KHR_shader_float_controls` | All four are enumerated, queryable, and accepted at device creation | Pass |
| Core/Features2 | 29 mandatory feature bits | Eight core feature bits plus `hostQueryReset` are true | 20 gaps |
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
eden-profile: extensions=0 features=19 limits=0 queues=0 total=19
```

The 19 feature gaps are `drawIndirectFirstInstance`, `dualSrcBlend`,
`fragmentStoresAndAtomics`, `imageCubeArray`, `largePoints`,
`multiDrawIndirect`, `multiViewport`, `robustBufferAccess`,
`sampleRateShading`, `shaderClipDistance`, `shaderCullDistance`,
`shaderImageGatherExtended`,
`shaderStorageImageWriteWithoutFormat`, `vertexPipelineStoresAndAtomics`,
`wideLines`, `shaderDemoteToHelperInvocation`, `shaderDrawParameters`,
`variablePointers`, and `variablePointersStorageBuffer`.

These bits must only be enabled as their complete Vulkan and shader semantics
become hardware-qualified. Eden continuing after logging an unsuitable driver
does not turn a failed requirement into support.

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
advertised and accepted through legacy and Features2 paths; dual-source blend
remains separate and unsupported. The public-path Prospero ELF SHA-256 is
`e42014fcab89df6001555faecd6a2c4a0d05edb87d9d7bcd011c62ca0caa6a99`.

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

1. Preserve all four completed mandatory extension contracts. Driver and
   conservative float-control properties are query-complete, and mirror clamp
   plus vertex divisors are hardware-qualified. Float execution-mode
   capabilities remain conservatively false.
2. Promote mandatory feature groups only after command/compiler coverage and
   hardware qualification; prioritize the existing OpenAGC raster, blend,
   indirect-draw, query, and shader paths. Sampler-anisotropy descriptor
   semantics and the core feature contract are implemented, host-tested, and
   hardware-qualified by paired bilinear/16x filtering readback on FW 5.50.
   Indirect draw recording is no longer a stub. Compiler metadata includes DrawIndex,
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
