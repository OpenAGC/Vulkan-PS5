# General-Purpose Vulkan-PS5 ICD

## Progress

- Milestone 6 `sampleRateShading` closure is complete for 4x optimal RGBA8
  color attachments. Full 4-iteration and partial 2-iteration semantics pass
  38/38 normal and sanitizer tests, VVL resource coverage, and repeated exact
  FW 5.50 SSBO gates. Two Eden feature gaps remain: `imageCubeArray` and
  `multiViewport`.

- Milestone 6 `robustBufferAccess` closure is complete. Byte-bounded raw
  UBO/SSBO descriptors and per-attribute vertex bounds pass 38/38 normal and
  sanitizer tests plus repeated compute and sparse-vertex FW 5.50 gates. Three
  Eden feature gaps remain.

- Milestone 6 `shaderStorageImageWriteWithoutFormat` closure is complete.
  Linear RGBA8 storage images, standard descriptor updates, formatless SPIR-V
  image stores, host/sanitizer coverage, and two bounded FW 5.50 exact-readback
  runs all pass. Four Eden feature gaps remain.

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
  Two independent FW 5.500.008 runs passed every deterministic oracle
  (`20260728T043915Z-tessellation-run1.log` and
  `20260728T044035Z-tessellation-run1.log`): the hull markers and copied LDS
  positions matched, TES wrote marker `0x54455300` and copied all three exact
  offchip positions, and each image contained exactly 7200 green pixels. Both
  processes returned normally, bounded post-run websrv probes confirmed the
  console remained responsive, and neither candidate was retried. This closes
  the repeated qualification gate for the correction and public
  `tessellationShader` feature path at the current standalone scope.
  Geometry's already qualified shader path is now promoted through the same
  standard feature contract: both feature-query forms report
  `geometryShader`, legacy and Features2 device creation accept it, and the
  standalone sample queries and requests it. Lifecycle regressions cover both
  acceptance forms and unsupported `wideLines` rejection. Both seven-test host
  configurations and the Prospero build pass with
  `-lunwind -lc++abi -lc++ -lm`; candidate ELF SHA-256 is
  `386aae854e1aaf504a750aa29904c491e35220d52c718c3bcf048f54de6803a4`.
  Two bounded FW 5.50 runs after independent console-availability signals are
  the remaining geometry feature gate before Milestone 4 WSI work resumes.
  Two independent bounded FW 5.500.008 runs produced exactly 4608 green pixels
  each (`20260728T051424Z-geometry-run1.log` and
  `20260728T051510Z-geometry-run1.log`). Both returned normally, bounded
  post-run websrv checks confirmed the console remained responsive, and
  neither was retried. This closes the public `geometryShader` feature gate;
  Milestone 4 VideoOut WSI work can now resume.
  Milestone 4 now has a host-qualified implementation: standard headless
  surface and swapchain entrypoints expose only a fixed 1920x1080 BGRA8-sRGB
  FIFO mode, allocate three direct write-combined scanout images, and route all
  firmware patching, registration, event queues, bounded flip waits, and
  teardown through OpenAGC. The direct WSI test covers exhaustion,
  synchronization, device groups, and recreation; all nine ICD tests, the
  runner safety simulation, and the WSI-enabled VVL test pass without messages.
  The standalone sample completes
  1,800 host frames and its Prospero ELF links with
  `-lSceSystemService -lunwind -lc++abi -lc++ -lm`; candidate SHA-256 is
  `d94722b2c9473b8407769b9b1fe044dd5796c6d5f78bbba7ccec15cfb6975c90`.
  The first bounded FW 5.50 run exited before buffer registration. The kernel
  log identified `SYSTEM_XO_VIOLATION` at
  `libSceVideoOut.sprx+0x7e61`: OpenAGC read the expected instruction bytes
  while the page was still execute-only. OpenAGC `290213c` now lifts the page
  to RWX before verification and restores RX on both mismatch and success.
  The console recovered, ps5debug-NG confirmed no qualification process
  survived, and no retry occurred. A second bounded run reached 1,800 frames,
  but its early PASS preceded a teardown SIGSEGV and `0x4000` VM resource leak.
  OpenAGC `18011af` now follows the hardware-proven teardown order by closing
  VideoOut before deleting the equeue, without an unqualified explicit
  flip-event deletion. The sample now prints PASS only after all Vulkan cleanup
  completes. The next bounded run reached all cleanup checkpoints and printed
  PASS, but returning from the ELF entrypoint jumped to `main+0xbb` and caused
  SIGSEGV. Subsequent `thr_exit` and libc `exit` candidates completed Vulkan
  cleanup but did not release the raw-ELF application lifecycle. The libc run
  (`20260728T060157Z-swapchain-run1.log`) left PID 145/app ID `0x16` on a black
  screen. A guarded `sceSystemServiceKillApp` recovery restored the home screen
  and was visually confirmed. The sample now resolves its app ID through
  `sceSystemServiceGetAppStatus`, requests app-level termination only after
  Vulkan cleanup, and remains alive until SystemService finishes the teardown.
  The helper is declared `_Noreturn`; the current Prospero disassembly ends
  `main` with its call plus `ud2`, and both helper branches loop on
  `sceKernelUsleep`, proving that the ELF cannot return to the raw loader.
  The next bounded run (`20260728T062155Z-swapchain-run1.log`) completed all
  1,800 frames and Vulkan cleanup. Klog recorded the self-requested `KillApp()`,
  `All processes exited`, and shell focus restoration; the exact PID was absent
  and websrv remained responsive. The remaining `0x4000` VM warning exactly
  matched OpenAGC's standalone multi-submit trailer allocation. OpenAGC
  `1c0fb8f` now places that 64-byte trailer in unused `SceGnmDdid` space.
  The follow-up (`20260728T063200Z-swapchain-run1.log`) reproduced the warning
  after another otherwise clean 1,800-frame lifecycle, falsifying the trailer
  hypothesis. OpenAGC `0c22e06` now restores the VideoOut text range to its
  exact original execute-only protection instead of read/execute, allowing the
  kernel to coalesce the temporary writable mapping.
  The next gate (`20260728T063634Z-swapchain-run1.log`) reproduced the warning,
  falsifying that hypothesis too. All 27 retained OpenAGC graphics klogs carry
  the identical one-page warning. OpenAGC `4f66aa7` now explicitly unregisters
  the flip event before closing VideoOut and deleting its still-live equeue.
  The balanced-lifecycle run (`20260728T064111Z-swapchain-run1.log`) again
  completed 1,800 frames and the full safe app-exit lifecycle without a crash,
  but retained the warning. A new SystemService-only probe contains no Vulkan,
  OpenAGC, GPU, VideoOut, equeue, or custom-memory operations and has a bounded
  runner that accepts only a clean lifecycle or exactly one matching baseline
  warning. Its ELF SHA-256 is
  `e585e74f872a4dfc7fa63910437b106843334666157672f9959c27558afe06a9`.
  The bounded baseline run at
  `20260728T064628Z-system-exit-probe-target.klog` produced that exact warning
  with no Vulkan, OpenAGC, GPU, VideoOut, equeue, or custom-memory use while
  completing self-KillApp, PID removal, and the console probe. This classifies
  the line as FW 5.50/raw-ELF bookkeeping and closes the Milestone 4 hardware
  gate with the balanced 1,800-frame swapchain evidence.
  A separately built recovery payload refuses to act unless exactly one other
  `eboot.elf` exists. The runner has no automatic retry and uses exact-PID
  ps5debug-NG cleanup only on a timeout/failure. The runner takes a bounded post-run
  klog snapshot, scopes it to the new eboot PID, rejects fatal signals, app
  crashes, XO faults, or warnings beyond the proven single raw-ELF baseline,
  requires a self-requested kernel `KillApp()`
  followed by `All processes exited`, and requires ps5debug-NG to prove process
  absence before reporting
  qualification PASS. Post-PASS safety failures also trigger exact-PID cleanup.
  Exhausted acquisition now waits against a monotonic deadline instead of
  returning early, while present releases the swapchain lock during the
  bounded VSYNC wait. A host regression holds all three images, presents one
  from another thread after 10 ms, and proves a waiting acquire wakes with the
  released image.
  Milestone 5 package closure is now host- and cross-build-qualified. A fresh
  install/relocation test moves Vulkan-Headers, OpenAGC, openagc-psbc, and
  Vulkan-PS5 before configuring a separately copied consumer. That consumer
  includes only `<vulkan/vulkan.h>`, finds only `VulkanPS5::ICD`, and exercises
  a standard instance/device lifecycle. The harness rejects source-workspace
  paths in installed metadata and link commands. Host passes as part of the
  11/11 suite; the Prospero run proves relocated archive use plus transitive
  `kernel`, `SceAgcDriver`, `SceVideoOut`, `unwind`, `c++abi`, `c++`, and `m`
  links. The retained installed-package ELF SHA-256 is
  `3da3698026eb62d5a97aedb8aa806ee0c6bc18469aa053ac32cc7caa16deb635`.
  Its dedicated one-shot runner passes a safety simulation and requires
  the standard Vulkan PASS oracle, self-KillApp ordering, exact-PID removal,
  post-run websrv availability, and no kernel warning beyond the proven single
  raw-ELF `0x4000` baseline.
  The retained FW 5.500.008 run executed once after a fresh availability
  signal and passed: PID 153 printed `package-consumer: PASS result=0`, the
  kernel recorded self-KillApp followed by `All processes exited`, ps5debug-NG
  proved the exact PID absent, and websrv remained responsive. The scoped klog
  contained only the proven raw-ELF warning. Evidence is retained at
  `20260728T070752Z-package-consumer.log` and
  `20260728T070752Z-package-consumer-target.klog`. This closes Milestone 5.
  Milestone 6 now has an automated Eden suitability baseline derived from
  `../eden-ps5` revision `39763e7321`. Vulkan 1.1, all four explicit limits,
  the universal queue, swapchain, geometry, tessellation, and host query reset
  pass. Four mandatory extensions and 26 mandatory features remain, for 30
  hard startup gaps. The reporting probe names each gap and supports a strict
  nonzero mode for closure. The accompanying matrix also records the runtime
  VMA, format, shader, presentation, and allowed Eden-integration work without
  weakening or special-casing Eden's requirements.
  The first application-neutral reduction implements the query-only
  `VK_KHR_driver_properties` and `VK_KHR_shader_float_controls` contracts.
  Driver metadata reports the experimental ACO/gfx1013 identity with
  conformance `0.0.0.0`; float execution-mode capabilities stay false until
  separately qualified. Lifecycle and VVL tests cover enumeration,
  Properties2 output, and device enablement. The precise-occlusion contract is
  also promoted from the already-qualified exact ZPASS path: repeated FW 5.50
  runs returned `samples=18432 green=18432`, and the query sample now queries,
  enables, and records `VK_QUERY_CONTROL_PRECISE_BIT` normally. Legacy and
  Features2 reporting plus device enablement are host-tested. The live profile
  is 27 gaps: two extensions and 25 features.
  The next extension gate is prepared without changing public capabilities:
  `vulkan_ps5_mirror_clamp_probe` samples a deliberately out-of-range
  coordinate and distinguishes gfx1013 mirror-once from ordinary edge clamp
  by deterministic readback. `run_fw550_mirror_clamp.sh` permits exactly one
  launch after a fresh console signal, scopes crash/warning checks and cleanup
  to the launched PID, and requires post-run process absence and websrv
  responsiveness. Its safety harness passes, as do all 13 host tests and the
  Prospero build. Candidate SHA-256 is
  `8ffe2a48c074391e0e96c56d03699a9f887b21ae0b15721be2d89a0cf24fe5da`;
  `VK_KHR_sampler_mirror_clamp_to_edge` remains hidden until that candidate
  passes one bounded FW 5.50 run.
  The other remaining extension now has its application-neutral compiler and
  pipeline foundation. openagc-psbc API v8 carries vertex/instance input rate
  and a per-attribute divisor into RADV's gfx1013 input lowering; Vulkan-PS5
  consumes `VkPipelineVertexInputDivisorStateCreateInfoEXT`, applies the
  default divisor of one to instance bindings, rejects zero divisors while the
  zero-divisor feature is disabled, and supplies base instance at draw time.
  Compiler tests, the Vulkan pipeline test, all 13 host tests, and both
  Prospero builds pass. Public extension properties, features, and enumeration
  remain disabled until deterministic hardware readback qualifies the path.
  That readback gate is now prepared. Four overlapping instances select four
  distinct texels; divisor two must make the final instance select exact white,
  while divisor one selects blue and vertex-rate interpretation interpolates.
  The one-shot runner uses the same exact-PID crash, cleanup, warning, and
  post-run console checks as the mirror-clamp gate. Its safety test and all 14
  host tests pass, and the Prospero candidate SHA-256 is
  `445ef566deba600cede29ef1d08bd19fbe6d0a14974fcb074d7afcdee8fcd4bf`.
  Public advertisement still awaits one fresh-console FW 5.50 run.
  Eden's allocator contract now has a real, external-style VMA consumer rather
  than direct-Vulkan approximations. The test uses dynamic Vulkan function
  lookup, external synchronization, Eden's 64/256 MiB block preference, mapped
  upload/download/stream policies, device-local buffer and image policies,
  within-budget allocation, block suballocation, and both manual allocate/bind
  paths. `VULKAN_MEMORY_ALLOCATOR_ROOT` is test-only and configurable; VMA 3.3
  uses Eden's exact AUTO manual policy, while VMA 3.4 uses its required explicit
  equivalent. Both direct-static and loader/VVL modes pass, and the full host
  suite is 17/17. A Prospero VMA consumer now builds and links through the
  exported ICD dependencies, including `-lunwind -lc++abi -lc++ -lm`; its
  SHA-256 is
  `8c7d669a9bef3acfcb1054eeb0327e4d51d645338374fa1202a9e640dd5f2871`.
  The first hardware attempt completed every VMA oracle, then exposed the raw
  Prospero C++ return-path fault at `RIP 0x4000bb`. The probe now terminates
  through application-level SystemService after flushing PASS, bypassing that
  broken destructor-dispatch path. Its next single bounded FW 5.50 run passed,
  removed exact PID 155, kept websrv responsive, and contained only the proven
  raw-ELF `0x4000` warning. The Eden allocator runtime patterns are therefore
  hardware-qualified at this scope.
  Sampler anisotropy now has complete hidden-path descriptor semantics without
  premature advertisement. Vulkan validates the enabled ratio against the
  reported 1x-16x range, selects gfx1013 anisotropic point/linear filter modes,
  and encodes the quantized maximum through OpenAGC's existing typed sampler
  API. The deterministic probe renders equal bilinear-control and 16x
  anisotropic triangles over a repeated one-pixel stripe texture. Both use the
  same elongated implicit-derivative footprint; mapped readback requires equal
  coverage, neutral means, substantial control aliasing, and at least a 25%
  reduction in mean absolute deviation for the anisotropic half. Its bounded
  runner rejects a missing oracle, exact-PID crash, unexpected warning,
  lingering process, or unavailable websrv. Host sampler tests plus runner
  clean/crash simulations pass in the 18/18 suite; the Prospero candidate links
  `-lunwind -lc++abi -lc++ -lm` and has SHA-256
  `1e7cfcaa9bcf6ca0c9afd1ede0d6ae519888b5db832a4d44deb7f68e3519a0f5`.
  The feature remains false until one fresh-console hardware gate qualifies the
  filtering result and the standard feature-query/request path is promoted.
  Vulkan indirect graphics commands now record through OpenAGC's validated
  gfx1013 wrappers. openagc-psbc API v9 exposes consecutive BaseVertex,
  BaseInstance, and DrawIndex user-SGPR metadata. Because the Mesa-style
  10-dword multi packet caused a bounded FW 5.50 GPU fault/reset on 2026-07-28,
  OpenAGC retains its previously qualified 7-dword PS5 packet. Vulkan expands
  DrawIndex-using multi draws into single-indirect packets and programs the
  DrawIndex SGPR per command; native multi remains available when DrawIndex is
  unused. Host recording locks three non-indexed and two indexed single packets
  for the DrawIndex shader, alongside range rejection. The probe checks
  `firstVertex = 1`, `firstInstance = 1,2`, and `gl_DrawID = 0,1` through exact
  green/blue readback. All 19 host tests pass; the Prospero ELF links
  `-lunwind -lc++abi -lc++ -lm` and has SHA-256
  `cbd05b90dc471644f7e278236abff26845124e69cb0562d8d7727f650b0e87b8`.
  `multiDrawIndirect`, `drawIndirectFirstInstance`, and
  `shaderDrawParameters` remain false until a fresh bounded run passes.

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

### Milestone 6 current progress

- `vertexPipelineStoresAndAtomics` is hardware-qualified through one combined
  VS/TCS/TES/GS pipeline. The fused primitive recorder now permits and
  preserves its vertex resource table before descriptor-set tables. Both
  33/33 host suites, the full Prospero build, and repeated public FW 5.50 gates
  pass the exact eight-marker and 7,200-green-pixel oracle. Evidence is
  retained in `20260728T133417Z-vertex-pipeline-stores-atomics-run1.log` and
  `20260728T133515Z-vertex-pipeline-stores-atomics-run1.log`; the public ELF
  SHA-256 is
  `e79e33fe4bc5c8f780e1801456e3ea9bae4a1034148d873b039563ac11dd171a`.
- `variablePointers` and `variablePointersStorageBuffer` are hardware-qualified
  through a bounded 64-invocation SPIR-V probe covering both OpSelect branches
  for StorageBuffer loads/stores and Workgroup stores/loads. The root cause of
  the earlier corruption was a Wave32 compiler/Wave64 dispatch mismatch;
  Vulkan now sets OpenAGC's named Wave32 compute modifier. Both 34/34 host
  suites and repeated public FW 5.50 gates pass. Evidence is retained in
  `20260728T144957Z-variable-pointers-run1.log` and
  `20260728T145026Z-variable-pointers-run1.log`; the public ELF SHA-256 is
  `d6d0669f82d2fcd7bac06099eaee6aa9511c8620744548d0a952001779d2702f`.
- `shaderStorageImageWriteWithoutFormat` is hardware-qualified through a
  standard compute pipeline and linear RGBA8 storage image. Both 36/36 host
  suites and repeated FW 5.50 gates pass the exact 4,096-pixel checkerboard
  oracle. Evidence is retained in `20260728T154623Z-storage-image-run1.log`
  and `20260728T155150Z-storage-image-run1.log`; the public ELF SHA-256 is
  `5234ca8640902545ea1c6c55bfe2f503365c3119678fb9ad4d030c51d96ed39a`.
- `robustBufferAccess` is hardware-qualified for both buffer descriptors and
  vertex fetch. Repeated bounded compute gates prove OOB SSBO reads return zero
  and stores are discarded; repeated sparse-binding vertex gates produce an
  exact 18,432-pixel blue triangle from a zero-record OOB `vec2` attribute.
  Evidence is retained in `20260728T161337Z-robust-buffer-run1.log`,
  `20260728T161845Z-robust-buffer-run1.log`,
  `20260728T161642Z-robust-vertex-run1.log`, and
  `20260728T161917Z-robust-vertex-run1.log`.
- `sampleRateShading` is hardware-qualified at both 1.0 and 0.5 minimum rates
  on a 4x RGBA8 attachment. Repeated full-rate gates produce stable sample-ID
  counts `18,336/18,528/18,432/18,432` and 73,728 total invocations; repeated
  partial-rate gates produce exactly 36,960 invocations with intact guards.
  Evidence is retained in `20260728T164229Z-sample-rate-shading-run1.log`,
  `20260728T164256Z-sample-rate-shading-run1.log`,
  `20260728T164808Z-partial-sample-rate-shading-run1.log`, and
  `20260728T164841Z-partial-sample-rate-shading-run1.log`.
- The live Eden compatibility profile is now two feature gaps:
  `imageCubeArray` and `multiViewport`.

## Test Plan and Assumptions

- Preserve the existing passing OpenAGC, openagc-psbc, and Vulkan-PS4 baselines.
- Add unit tests for allocation failure, pNext chains, descriptors, specialization constants, render passes, queries, synchronization, pipeline caches, swapchain recreation, and concurrent queue/API use.
- Run Vulkan Validation Layers with zero errors or warnings across standalone graphics, compute, transfer, and WSI applications.
- Add targeted Vulkan CTS/deqp coverage for every advertised Vulkan 1.1 feature and extension. Report a non-conformant conformance version until qualification supports a stronger claim.
- Require deterministic GPU readback and repeated FW 5.50 runs before advertising each hardware feature.
- Eden-specific source changes are limited to PS5 surface creation, build/link integration, and locating the statically linked Vulkan entrypoint. Eden's renderer continues using standard Vulkan.
- Other applications may either link `libvulkan_ps5.a` directly or consume the exported CMake package; they do not depend on Eden.
