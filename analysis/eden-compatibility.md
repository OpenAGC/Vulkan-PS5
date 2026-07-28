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
both components build for Prospero. A deterministic hardware readback probe,
followed by extension properties/features and enumeration, is still required.
The probe and one-shot runner are now prepared: four overlapping instances
must resolve to an exact-white triangle only when divisor two is honored. Its
runner safety regression and the full 14-test host suite pass. Prospero
candidate SHA-256 is
`445ef566deba600cede29ef1d08bd19fbe6d0a14974fcb074d7afcdee8fcd4bf`;
one fresh-console FW 5.50 run remains before public promotion.

The automated probe currently reports:

```text
eden-profile: extensions=2 features=25 limits=0 queues=0 total=27
```

The 25 feature gaps are `depthBiasClamp`, `depthClamp`,
`drawIndirectFirstInstance`, `dualSrcBlend`, `fillModeNonSolid`,
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

## Runtime compatibility

| Area | Current evidence | State |
| --- | --- | --- |
| VMA | A configurable VMA consumer matches Eden's dynamic functions, external synchronization, upload/download/stream/device-local policies, images, manual bind, and block suballocation; direct and loader/VVL modes pass; one bounded FW 5.50 run passed every oracle and exited through SystemService with exact-PID removal | Hardware-qualified at this scope |
| Formats | Eden snapshots roughly 150 guest-relevant formats. The ICD currently exposes 19 uncompressed/depth formats, no BC formats, no D24, and no storage-image feature bits | Major gap |
| Shader pipelines | VS/FS/CS/GS/tessellation, descriptors, specialization constants, push constants, vertex input, MRT, depth/stencil, and queries have qualified paths | Mandatory shader capabilities above remain incomplete |
| Indirect draws | Single/multi indexed and non-indexed commands record validated gfx1013 PM4 through OpenAGC; corrected one-draw BaseVertex/BaseInstance hardware readback passed without a GPU reset | Initiator hardware-qualified at one-draw scope; clean SystemService lifecycle rerun and complete multi/DrawID gate pending |
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
   A clean lifecycle rerun and the complete multi-draw/DrawID gate remain before
   the three related core bits can be promoted. The rejected packet experiments,
   GPU-reset evidence, root cause, and runner hardening are documented in
   `fw550_indirect_draw_parameters_20260728.md`.
3. Expand qualified uncompressed and BC format support, with D24 fallback kept
   honest and ASTC/ETC remaining unsupported until conversion is implemented.
4. Add only the allowed Eden changes: Prospero surface creation, build/link
   integration, and static Vulkan entrypoint location.
