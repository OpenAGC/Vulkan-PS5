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

The automated probe currently reports:

```text
eden-profile: extensions=2 features=26 limits=0 queues=0 total=28
```

The 26 feature gaps are `depthBiasClamp`, `depthClamp`,
`drawIndirectFirstInstance`, `dualSrcBlend`, `fillModeNonSolid`,
`fragmentStoresAndAtomics`, `imageCubeArray`, `independentBlend`, `largePoints`,
`logicOp`, `multiDrawIndirect`, `multiViewport`, `occlusionQueryPrecise`,
`robustBufferAccess`, `samplerAnisotropy`, `sampleRateShading`,
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
| VMA | Vulkan 1.1 memory-requirements/bind v2 entrypoints and two device-local host-visible heaps exist; the installed-package consumer does not exercise Eden's VMA allocation patterns | Dedicated consumer coverage missing |
| Formats | Eden snapshots roughly 150 guest-relevant formats. The ICD currently exposes 19 uncompressed/depth formats, no BC formats, no D24, and no storage-image feature bits | Major gap |
| Shader pipelines | VS/FS/CS/GS/tessellation, descriptors, specialization constants, push constants, vertex input, MRT, depth/stencil, and queries have qualified paths | Mandatory shader capabilities above remain incomplete |
| Presentation | Standard headless surface plus FIFO swapchain is hardware-qualified for 1,800 frames | Eden PS5 surface hookup missing |

## Implementation order

1. Complete the two remaining mandatory extension contracts: sampler mirror
   clamp and vertex divisors with real sampler/pipeline semantics. The
   query-only driver/float-control contracts are complete and tested; float
   execution-mode capabilities remain conservatively false. The mirror-clamp
   hardware probe and bounded runner are prepared; public enumeration awaits
   its single FW 5.50 qualification run.
2. Promote mandatory feature groups only after command/compiler coverage and
   hardware qualification; prioritize the existing OpenAGC raster, blend,
   indirect-draw, query, and shader paths.
3. Add a standalone VMA consumer matching Eden's externally synchronized,
   dynamic-function allocator and its upload/download/device-local patterns.
4. Expand qualified uncompressed and BC format support, with D24 fallback kept
   honest and ASTC/ETC remaining unsupported until conversion is implemented.
5. Add only the allowed Eden changes: Prospero surface creation, build/link
   integration, and static Vulkan entrypoint location.
