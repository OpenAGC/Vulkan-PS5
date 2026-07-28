# General-Purpose Vulkan-PS5 ICD

## Progress

- Milestone 1 is complete: host lifecycle, dispatch, conservative gfx1013
  capabilities, memory/resources, packaging, and loader/VVL tests.
- Milestone 2 compiler-library integration is complete: reusable host/Prospero
  archives and VS/PS/CS/GS/tessellation Vulkan pipeline creation cover fused-stage
  specialization constants, descriptors, push constants, vertex input, and
  render-pass context.
- Milestone 3 is complete: host tests verify OpenAGC DCB emission for a
  compiled compute dispatch and fused-NGG triangle draw, plus GPU-visible shader
  uploads, bounded EOP-backed OpenAGC queue submission, and compute
  uniform/storage-buffer resource tables. The first compute dispatch per
  command buffer also emits the complete OpenAGC gfx1013 compute-default
  register groups. Deterministic standalone compute and
  triangle readback applications now cross-link for Prospero, and the triangle
  path emits render-target transitions, frame state, and attachment binding.
  OpenAGC now owns the FW 5.50 GPU process-authorization setup used by ordinary
  Vulkan applications. Two consecutive FW 5.50 runs of all three samples
  passed: compute verified all 1,024 values, triangle verified exactly 18,432
  green pixels, and indexed-textured verified 18,432 opaque pixels with 64+
  sampled colors per run. Linear images expose and use the gfx1013 256-byte row
  pitch. Host command tests now cover OpenAGC-laid-out optimal D32 images,
  depth attachment transitions, and static depth-test/write plus front/back
  stencil PM4 on combined D32+S8 planes. Dual-target command recording also
  verifies CB1 binding, per-attachment transitions, and packed fragment-export
  state. A standalone two-target mapped-readback sample now exercises the MRT
  path. GPU-backed occlusion pools now cover reset, begin/end, bounded host
  retrieval, partial results, and availability. A standalone query workload
  cross-checks its 64-bit occlusion result against mapped triangle coverage.
  The planned `VK_EXT_host_query_reset` surface is also implemented and tested.
  FW 5.50 has now passed repeated depth, MRT, and occlusion-query readback.
  Query recovery isolated and qualified lifecycle, corrected command reset,
  idle begin/end, and live counting before the final full suite. The retained
  `20260728T003630Z` gate passed compute, triangle, indexed-textured, depth,
  MRT, and query twice each with exact deterministic oracles.
  Completion audit extended the repeated depth gate to cover S8 hardware, not
  only host-verified stencil PM4. The combined D32+S8 sample passed twice with
  22,118 `REPLACE 0x5a` writes exactly matching color coverage; the expanded
  `20260728T003956Z` full gate passed all 12 runs.
  Geometry draw recording and its deterministic standalone Prospero ELF passed
  twice on FW 5.50. Tessellation diagnostics safely fixed missing DCB ring
  programming and separated the basic OpenAGC-qualified shader dataflow from
  still-unqualified offchip patch-output reads. The revised basic gate passed
  twice on FW 5.500.008 with exactly 7200 pixels, completing standalone geometry
  and basic tessellation hardware qualification. Geometry can now advance
  through its standard feature-request path; tessellation stays disabled until
  patch-output reads are fixed and hardware-qualified as well. Linked
  TCS/TES interfaces and a nonzero hull-LDS allocation have each been exercised
  by one bounded FW 5.500.008 run; both returned safely but produced a zeroed
  target, so neither candidate was retried or qualified. The next local
  candidate corrects gfx10.3's two-step 1024-byte-allocation/512-byte-encoding
  rule. Its single bounded run also returned safely with a zeroed target, so it
  was not retried and tessellation feature exposure remains blocked on the
  separate LS-front/HS-back dataflow. Local ACO inspection then identified the
  dataflow defect: the independently compiled HS back retained RADV's
  monolithic same-invocation temporary-VGPR path and consequently wrote zero
  input positions. openagc-psbc now disables that shortcut in both halves,
  lowers the TCS inputs to shared LDS loads, and rejects surviving per-vertex
  HS inputs. Host compiler tests, all seven ICD tests, and the Prospero build
  pass. Its one bounded FW 5.50 run returned safely and left websrv responsive,
  but produced a zeroed target (`20260728T030535Z-tessellation-run1.log`). It
  was not retried. The next investigation is downstream of the fixed HS LDS
  loads: offchip ring configuration, HS stores, and TES consumption. Graphics
  uniform/storage-buffer descriptor encoding is now shared with compute and
  host-tested through a TCS binding, enabling a standard Vulkan hull-output
  readback oracle for the next materially distinct hardware candidate. That
  oracle is now implemented: independent execution words localize a missing HS
  launch, copied VS positions localize a broken LDS path, and a passing buffer
  alongside a black image isolates the remaining offchip/TES path. The host
  and Prospero builds passed, but its one bounded FW 5.50 launch faulted an SQC
  data read at unmapped `0x2_00000000`. Kernel evidence and ACO agree that the
  HS indirect descriptor-set-table SGPR was zero. openagc-psbc API v7 now
  exposes that ABI and the ICD supplies a GPU-visible set-pointer table; host
  tests prove the exact SGPR/table-address pair reaches PM4 and reject pointers
  outside gfx1013's `0x2_xxxxxxxx` aperture. The materially distinct corrected
  ELF completed its first bounded FW 5.500.008 run successfully: all three hull
  invocation markers and copied control-point positions matched, and the image
  contained exactly 7200 green pixels
  (`20260728T034030Z-tessellation-run1.log`). No retry was attempted. A second
  independent run after a fresh console-availability signal reproduced every
  hull and image oracle (`20260728T034211Z-tessellation-run1.log`) and left the
  console responsive. Patch-output tessellation is now hardware-qualified at
  this scope. The ICD now advertises `tessellationShader` through both physical
  device feature queries, accepts that feature through legacy and Features2
  device creation while continuing to reject every unadvertised core feature,
  and makes the standalone sample request it explicitly. Both seven-test host
  configurations and the Prospero cross-build pass. Its one bounded hardware
  smoke preserved all hull markers and copied control points but produced an
  all-zero image (`20260728T034904Z-tessellation-run1.log`). The application
  returned and the console remained responsive; no retry was attempted. The
  passing and failing logs are identical until the image oracle, exposing
  nondeterminism after TCS execution in the offchip-to-TES/raster path.
  `tessellationShader` exposure must remain unqualified until that path is
  corrected and the repeated gate passes again. The next materially distinct
  diagnostic extends the same standard storage buffer into TES. A uniquely
  selected tessellated vertex records a TES marker and copies all three
  offchip control points, separating missing TES launch, bad offchip reads,
  and downstream rasterization. Both host configurations pass all seven tests,
  host execution reaches the expected no-GPU oracle, and the Prospero build
  passes. Its one bounded FW 5.500.008 run returned normally and left the
  console responsive (`20260728T035640Z-tessellation-run1.log`). All three
  hull markers and copied LDS positions passed, and TES wrote marker
  `0x54455300`, proving that the factor ring launches evaluation. TES read all
  three offchip control-point positions as zero and the image stayed black.
  The remaining defect is therefore localized before rasterization, to the HS
  offchip stores or the matching TES offchip address/layout ABI. No retry was
  attempted.
  OpenAGC commit `6406c9b` corrects a capacity mismatch discovered in that
  path: the previous profile exposed one 8K-dword (32 KiB) offchip buffer with
  `VGT_HS_OFFCHIP_PARAM = 0`, while the gfx1013 topology and Mesa policy require
  four resident offchip workgroups per CU across four shader engines, two
  shader arrays per engine, and five CUs per array. The replacement profile
  uses 160 buffers, a 5 MiB offchip ring, encoded buffer count `159`, and a
  `0x1e000` factor ring. OpenAGC validation now rejects size/profile mismatch.
  Both Vulkan host configurations pass all seven tests, the Prospero build
  passes with `-lunwind -lc++abi -lc++ -lm`, and candidate ELF SHA-256 is
  `316ee53df2a1b29d7dcd1c5f1c4adb3cfe0d0f07bb66f2ea41cb1d88eda9e09b`.
  One bounded FW 5.50 run is pending a fresh console-availability signal; the
  repeated gate remains required before the feature is considered stable.

## Summary

- Build Vulkan-PS5 as a reusable Vulkan 1.1 implementation for arbitrary PS5 homebrew, libraries, engines, and ports.
- Use OpenAGC as the hardware abstraction for `/dev/gc`, gfx1013 PM4, descriptors, submission, synchronization, and VideoOut. Do not duplicate those low-level facilities inside the ICD.
- Convert `openagc-psbc` into a runtime SPIR-V compiler library usable by every Vulkan application.
- Keep the implementation application-neutral and standards-based. Eden is a demanding compatibility workload and development guide, not a special backend or architectural dependency.
- Target FW 5.50 first, with FW 3.20 added after the primary path is stable.

## Public Architecture and Interfaces

### Reusable SDK

- Produce:
  - `libvulkan_ps5.a` for statically linked PS5 applications.
  - A loader-compatible host shared library for VVL and development.
  - A relocatable `VulkanPS5::ICD` CMake package.
  - Standard `vkGetInstanceProcAddr`, `vkGetDeviceProcAddr`, and Vulkan entrypoints.
  - Standalone compute, triangle, textured-cube, depth, and swapchain examples.
- Consumers use ordinary Vulkan headers and APIs. No Eden-specific types, entrypoints, descriptor conventions, shader ABI, or synchronization behavior enter the public interface.
- Dependency locations remain configurable for Vulkan-Headers, OpenAGC, and openagc-psbc. The PS5 SDK package exports transitive OpenAGC/system-library requirements.

### OpenAGC and shader compiler

- Add a reusable `libopenagc_psbc.a` API for host and Prospero. It accepts SPIR-V stages, entry points, specialization constants, vertex input, descriptor/pipeline layouts, and push constants; it returns AGC shader records, executable code, register state, resource-table mappings, and user-SGPR metadata.
- Compile shaders during pipeline creation because vertex layouts, tessellation, and geometry/NGG fusion require complete pipeline context.
- Extend OpenAGC with application-neutral helpers for raster state, topology, line state, depth bias, blend constants, stencil masks/references, queries, generalized transitions, copies, blits, resolves, clears, mip/layer layouts, and missing gfx1013 formats.
- Keep kernel ioctls, PM4 encodings, firmware profiles, and GPU register details behind OpenAGC APIs.

## Vulkan-PS5 Implementation

### Core driver

- Reuse Vulkan-PS4's API-neutral dispatch, object, allocator, descriptor, render-pass, pipeline-cache, and test patterns, but replace all PS4/GNM structures and commands with `VkPs5*` objects and OpenAGC operations.
- Implement the complete Vulkan 1.1 entrypoint surface, including pNext handling, memory-requirements/bind v2 calls, descriptor update templates, device groups, pipeline caches, queries, and legal unsupported responses for sparse or protected features.
- Report one gfx1013 physical device and an initial universal graphics/compute/transfer queue family. Add a separate async-compute family only after ACE submission is hardware-qualified.
- Derive physical-device features, limits, memory properties, and format support from actual gfx1013/OpenAGC capabilities. Never advertise placeholders solely to satisfy a particular application.
- Support normal uncompressed, depth/stencil, and BC formats first. Report ASTC, ETC, or unqualified formats unsupported until conversion or native support is validated.

### Resources, commands, and synchronization

- Use GPU-visible flexible memory for normal Vulkan allocations and direct write-combined memory for scanout. Support mapping, coherent flush/invalidate semantics, alignment, VMA block suballocation, allocation limits, and deterministic cleanup.
- Represent buffers and images with OpenAGC descriptors and complete mip, layer, aspect, and layout metadata.
- Record Vulkan commands into OpenAGC DCBs. Use DMA for buffer transfers, OpenAGC transitions for barriers, and internal compute/graphics shaders for image operations without a direct packet path.
- Implement render passes, dynamic rendering-equivalent internal behavior, MRT, depth/stencil, clears, resolves, indirect draws, geometry, tessellation, compute, occlusion/timestamp queries, and pipeline statistics where supported.
- Implement binary fences and semaphores with GPU-visible labels and `RELEASE_MEM` EOP writes. Serialize queues with monotonic submission values and make host waits bounded and thread-safe.
- Keep timeline semaphores, sparse resources, transform feedback, descriptor indexing, and advanced optional extensions disabled until their complete semantics are implemented.

### Extensions and WSI

- Implement broadly useful initial extensions:
  - `VK_KHR_surface`
  - `VK_KHR_swapchain`
  - `VK_EXT_headless_surface`
  - `VK_KHR_driver_properties`
  - `VK_KHR_sampler_mirror_clamp_to_edge`
  - `VK_KHR_shader_float_controls`
  - `VK_EXT_vertex_attribute_divisor`
  - `VK_EXT_host_query_reset`
  - Required Vulkan 1.1 maintenance/property dependencies
- Treat a headless surface as the standard PS5 VideoOut surface, allowing any Vulkan application to create a swapchain without a platform-specific Vulkan header.
- Back swapchains with VideoOut-compatible triple-buffered direct memory. Guarantee FIFO presentation and advertise other modes only after hardware validation.
- Isolate the FW 5.50 credential setup, VideoOut registration patch, equeue handling, buffer registration, flip, patch restoration, and teardown inside the WSI platform module.
- Present only after the submission's GPU completion value is reached, using OpenAGC's EOP/flip facilities.

## Compatibility and Delivery Milestones

1. Host ICD lifecycle, loader dispatch, physical-device properties, memory, and VVL-clean object tests.
2. Runtime SPIR-V library for VS/PS/CS/GS/tessellation, descriptors, specialization constants, and push constants.
3. FW 5.50 standalone compute, indexed triangle, textured rendering, depth/stencil, MRT, query, geometry, and tessellation samples.
4. General VideoOut-backed Vulkan swapchain sample sustaining at least 1,800 frames with correct acquire/submit/present synchronization and clean teardown.
5. Reusable SDK package consumed by a separate, minimal PS5 homebrew project without source-tree-relative includes.
6. Eden compatibility profile: pass Eden's mandatory extension, feature, limit, format, queue, VMA, shader-pipeline, and presentation checks without application-specific driver behavior.
7. Run `../2048.nro` through `eden-ps5`, followed by broader applications and engines to prevent the implementation from overfitting to Eden.

## Test Plan and Assumptions

- Preserve the existing passing OpenAGC, openagc-psbc, and Vulkan-PS4 baselines.
- Add unit tests for allocation failure, pNext chains, descriptors, specialization constants, render passes, queries, synchronization, pipeline caches, swapchain recreation, and concurrent queue/API use.
- Run Vulkan Validation Layers with zero errors or warnings across standalone graphics, compute, transfer, and WSI applications.
- Add targeted Vulkan CTS/deqp coverage for every advertised Vulkan 1.1 feature and extension. Report a non-conformant conformance version until qualification supports a stronger claim.
- Require deterministic GPU readback and repeated FW 5.50 runs before advertising each hardware feature.
- Eden-specific source changes are limited to PS5 surface creation, build/link integration, and locating the statically linked Vulkan entrypoint. Eden's renderer continues using standard Vulkan.
- Other applications may either link `libvulkan_ps5.a` directly or consume the exported CMake package; they do not depend on Eden.
