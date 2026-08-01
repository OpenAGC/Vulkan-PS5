# General-Purpose Vulkan-PS5 ICD

## Authoritative Architecture and Migration Plan

Vulkan-PS5 remains a general-purpose Vulkan implementation for PS5 homebrew,
but its long-term hardware boundary is OpenAGC's native runtime rather than a
collection of direct low-level helpers. The current ICD and its FW 5.50 gates
are the qualified baseline. Do not discard or silently bypass them while the
native runtime is still under construction.

The dependency order is fixed:

1. OpenAGC first stabilizes firmware-neutral `AgcDevice`, `AgcQueue`, resource,
   shader, pipeline, command-buffer, fence, allocator, transition, validation,
   capture, and presentation contracts.
2. `openagc-psbc` supplies versioned compiler reflection consumed by those
   shader and pipeline objects.
3. Vulkan-PS5 translates Vulkan semantics to the stable native objects.
4. The ICD removes each superseded direct PM4, firmware, allocation,
   reflection, transition, synchronization, or VideoOut path only after the
   replacement passes equivalent host and hardware gates.

The public Vulkan surface stays standard. No OpenAGC handles, firmware keys,
cache-control words, Sony structures, or application-specific conventions may
leak through Vulkan entry points.

### Ownership boundary

OpenAGC owns:

- Runtime firmware/profile selection and capability qualification.
- Device/queue initialization and teardown.
- GPU heaps, suballocation, resource layouts, residency, staging, and deferred
  destruction.
- Shader records and validated native graphics/compute pipeline state.
- Command storage, PM4 emission, resource-state/cache transitions, submission,
  fences, waits/signals, timeout diagnostics, and capture records.
- Scanout-capable resources, VideoOut patch policy, acquire/present lifecycle,
  and exact-firmware safety gates.

`openagc-psbc` owns SPIR-V compilation and compiler-backed reflection for
descriptor tables, push constants, vertex inputs, builtins/user SGPRs, color
exports, wave size, scratch/LDS, tessellation, geometry/NGG, and stage linkage.

Vulkan-PS5 owns:

- Vulkan instance/device/object behavior, dispatch, `pNext`, allocation
  callbacks, and legal unsupported results.
- Feature, extension, limit, queue, format, and memory-property advertisement
  derived from native capabilities and Vulkan requirements.
- Vulkan memory requirements/binding rules, descriptors, render passes or
  dynamic rendering, pipeline layouts/caches, query semantics, and command-
  buffer state validation.
- Translation of Vulkan access masks, stages, image layouts, and queue-family
  ownership into supported native transitions.
- Vulkan fences, semaphores, submission ordering, swapchains, and WSI semantics
  implemented through native synchronization and presentation objects.

### Migration milestone 0: freeze and audit the baseline

Current progress: the mechanically checked direct-call inventory has zero
rows. The `vulkan_ps5.native_migration_audit` CTest gate rejects any
reintroduced low-level call. Historical category and sequencing evidence is
preserved in `analysis/native_migration_audit_20260731.md`.

Typed native indirect recording and occlusion-query storage/recording/results
are complete. The latter is FW 5.500.008-qualified with an exact
`samples=18432 green=18432` oracle and clean guarded lifecycle; neither path
retains Vulkan-side raw addresses or packet emission.

1. Record the current advertised Vulkan version, features, extensions, limits,
   queues, memory types, formats, and WSI modes as a machine-readable baseline.
2. Inventory every direct OpenAGC low-level call and classify it as lifecycle,
   memory/resource, shader/reflection, pipeline, command/PM4, transition,
   synchronization, query, or presentation ownership.
3. Identify duplicated firmware checks, address/layout calculations, command
   allocation, cache policy, fence labels, and cleanup paths.
4. Preserve normal, ASAN/UBSAN, loader/VVL, package-relocation, Prospero-build,
   and bounded FW 5.50 results before replacing a path.

Exit criteria: every hardware-facing call has a migration owner and a named
regression gate; no currently advertised capability is silently dropped.

### Migration milestone 1: device, capabilities, and resource memory

The logical-device contract gap is resolved in OpenAGC API 26. Its runtime
owns a process backend until the last independent `AgcDevice` is destroyed,
requires active devices to select the same AGC defaults version, and tracks
each generic compute queue by its exact backend handle. Vulkan now creates one
native device and native graphics/compute queue pair per `VkDevice`; its
lifecycle regression holds two Vulkan devices concurrently and proves the
survivor remains usable after peer teardown.

OpenAGC API 28 supplies explicit allocations, placed resources, image tiling,
typed/swizzled views, and normalized sampler state.
`VkDeviceMemory` and bound `VkBuffer` objects now use `AgcMemory` and
`AgcBuffer`; generic and sanitizer qualification must remain green while the
completed, FW 5.50-qualified image/view/sampler half remains under regression.

1. Create one `AgcDevice` per Vulkan device and map queues to `AgcQueue`.
2. Derive physical-device properties and format/feature qualification from
   `agcGetRuntimeInfo`; never branch on a firmware number in the ICD.
3. Map Vulkan allocations to OpenAGC heaps/suballocations while preserving
   Vulkan memory-type, alignment, mapping, flush/invalidate, aliasing, and
   memory-requirement rules.
4. Replace buffer/image/view/sampler backing with `AgcBuffer`, `AgcImage`,
   `AgcImageView`, and `AgcSampler` without changing Vulkan handle semantics.
5. Retain dedicated allocations where Vulkan or scanout constraints require
   them and defer resource retirement until the owning submission completes.

Exit criteria: lifecycle, VMA-pattern, resource, map/flush/invalidate, alias,
allocation-failure, and leak/high-water tests pass with no ICD-owned GPU heap
or duplicated image-layout calculator.

### Migration milestone 2: shader reflection and pipelines

Host-complete: OpenAGC API 36/PSBC API 18 now back all Vulkan shader stages,
all compute pipelines, and the advertised point, line, triangle, geometry,
and tessellation graphics forms with native objects. Native fixed state covers
polygon modes, culling, rasterizer discard, strip/fan primitive restart,
depth clamp, static/dynamic depth bias and line width, logic operations, and
pipeline switching. The compiler emits only descriptor sets
addressable by each stage while preserving every binding within those sets,
and explicitly marks alpha-to-one epilogs. Native command encoding consumes
these objects directly.

1. Translate Vulkan shader modules, specialization, vertex input, descriptor
   layouts, push constants, render-target/depth formats, blend/raster/depth,
   multisampling, and stage linkage into native pipeline descriptors.
2. Use the versioned `openagc-psbc`/OpenAGC reflection contract. Remove ICD-
   private guesses for user SGPRs, export formats, wave mode, descriptor-table
   registers, tessellation/offchip layouts, or fused stages.
3. Map pipeline-cache entries to stable shader/pipeline hashes and versioned
   native compatibility information.
4. Propagate precise pipeline-creation diagnostics while returning the Vulkan
   result required for invalid or unsupported combinations.

Exit criteria: graphics and compute pipeline tests cover positive and negative
shader/attachment, integer-blend, descriptor, sample-count, wave, and stage-
linkage cases; rejected pipelines emit no commands.

### Migration milestone 3: commands, transitions, and synchronization

Host-complete: each Vulkan command buffer owns paired queue-typed native command
buffers, but the graphics DCB is now the single ordered Vulkan stream and may
carry compute work. Their lifecycle is synchronized with Vulkan allocation,
begin/end rollback, reset, pool reset, free, and teardown. Compatible pipeline
binds, supported typed buffer/image barriers, and explicitly transitioned
buffer copies record natively. Compute descriptors with explicit compatible
resource state and their dispatches also record on the ordered graphics DCB.
Graphics descriptor/attachment/vertex/index binding, 16-entry static or
dynamic viewport/scissor state, baseline draws, and finite-wait native queue
submission now form an FW 5.500.008-qualified path. PSBC exposes an explicit
runtime API-version handshake so stale host/Prospero compiler archives fail
closed. OpenAGC APIs 36-38 now own typed indirect draw, indexed-draw, dispatch,
occlusion-query, physical-device-property, and buffer-copy recording, including
multi-draw DrawIndex expansion and argument-buffer retention; Vulkan's
superseded indirect/copy encoders have been deleted and the audit has fallen
from 45 to 37 calls. Native-only operations now latch the complete-stream
requirement in both command orderings so legacy fallback cannot silently omit
them. Kernel-submitted native command storage now uses dedicated mappings; the
first shared-heap native-copy candidate panicked FW 5.50, while the isolated
replacement passed the same cleanup-guarded two-region workload twice. The
native multi-draw path is FW 5.50-qualified by the deterministic
11,472-pixel two-draw oracle. OpenAGC API 41 now owns typed color/BC image
regions and buffer/image transfer records. `vkCmdCopyImage`,
`vkCmdCopyBufferToImage`, and `vkCmdCopyImageToBuffer` record through those
contracts, including partial mips/layers and explicit buffer row/image
strides. Clear, blit, depth/stencil-transfer, and resolve forms fail closed
instead of silently succeeding. Typed native buffer update and fill remain
complete in API 39. WSI uses dedicated native scanout images, `AgcPresentChain`,
and the ordered queue's retained completion fence; this removes the three raw
VideoOut symbols and reduced the audit to 34. Shader code allocation/fusion
and native-only direct draw/dispatch recording reduced it to 26. Descriptor
tables, image layouts, custom border tables, tessellation resources, command
storage, submission, and finite fence waits now belong to OpenAGC; deletion of
the superseded Vulkan encoder and fallback path reduces the audit to zero.

1. Back Vulkan command pools/buffers with native command allocators and
   `AgcCommandBuffer` state while retaining Vulkan reset and simultaneous-use
   rules.
2. Translate draw, indirect draw, dispatch, copy, clear, resolve, query, and
   dynamic-state commands to native operations. Do not retain a parallel PM4
   path after a complete native equivalent is qualified.
3. Translate Vulkan barriers and image layouts to typed native resource usages
   and subresource ranges. Reject a mapping that lacks a qualified native
   transition instead of inventing raw cache bits in the ICD.
4. Map queue submissions, multiple command buffers, fences, semaphores, and
   waits/signals to native synchronization with explicit finite host deadlines.
5. Preserve the host-tested Vulkan timeline-semaphore contract while migrating
   it to native counters; add queue families only after native ownership is
   complete and hardware-qualified.

Exit criteria: deterministic compute, graphics, indirect, transfer, depth,
MSAA, query, and compute-to-graphics workloads pass normal/sanitizer/VVL tests
and bounded endpoint gates without ICD-owned PM4 or fence-label code.

### Migration milestone 4: WSI, validation, and capture

1. **FW 5.50 complete:** map swapchain images and presentation to OpenAGC-owned
   scanout resources, transitions, fences, bounded flip waits, and teardown.
   The API-40 candidate passed 1,800 frames and clean lifecycle with SHA-256
   `0b1d87d02a5fbe480cc74890c613752bb55c2e7b5f4e729413314785e5302888`.
2. Keep Vulkan surface/swapchain semantics and application-facing image state
   in the ICD while all firmware patch and VideoOut hardware policy stays in
   OpenAGC.
3. Route native validation messages through Vulkan debug callbacks with stable
   object labels and Vulkan-command context.
4. Include Vulkan object and command identifiers in OpenAGC captures without
   embedding raw process pointers or treating a capture as automatically safe
   to replay.

Exit criteria: swapchain acquire/submit/present, exhaustion, recreation,
resize/lifecycle, timeout, and teardown gates pass repeatedly on FW 5.50 and FW
11.60 with no duplicated VideoOut patch or event-queue implementation.

### Migration milestone 5: feature profile and downstream qualification

1. Re-run every currently advertised feature and extension through the native
   path. Preserve fail-closed behavior for unqualified formats or operations.
2. Add focused CTS/deqp coverage for the advertised subset and keep the
   conformance version non-conformant until the required suite supports a
   stronger claim.
3. Run the installed-package consumer, focused standard Vulkan samples, and a
   game-like workload through one firmware-neutral build on FW 5.50 and FW
   11.60.
4. Treat Eden and other engines as application-neutral compatibility clients,
   never as sources of private driver behavior.

Exit criteria: the migrated ICD contains no second hardware backend, its
advertised matrix is backed by native OpenAGC qualification, and selected
unmodified Vulkan homebrew applications pass deterministic endpoint and long-
running lifecycle oracles.

## Existing Implementation and Qualification Ledger

The entries below record completed direct-integration work and hardware
evidence. They are regression requirements, not the authoritative order for
the native-runtime migration.

- Milestone 6 `multiViewport` closure is complete. The ICD exposes 16
  viewports, accepts static and dynamic viewport/scissor arrays, and emits
  OpenAGC's typed gfx1013 per-slot state after shader binds. Normal and
  sanitizer suites pass 39/39, VVL reports no diagnostics, the Prospero build
  passes, and two exact FW 5.50 gates produced 9,216 pixels in each of two
  independently indexed viewports. The automated Eden ICD profile has zero
  hard startup gaps.

- Milestone 6 `imageCubeArray` closure is complete. Standard 12-layer
  cube-compatible images, cube/cube-array/2D-array views, typed gfx1013 layer
  descriptors, runtime `samplerCubeArray` compilation, 39/39 normal and
  sanitizer tests, VVL coverage, and two repeated exact FW 5.50 readback gates
  pass. One Eden feature gap remains: `multiViewport`.

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
  `kernel`, `SceVideoOut`, `unwind`, `c++abi`, `c++`, and `m` links while
  rejecting an installed `SceAgcDriver` dependency. The retained
  installed-package ELF SHA-256 is
  `1fd79429140e26884cc492761d8551cf7a2b8769c39066463d9f05f6c9fc5547`.
  Its dedicated one-shot runner passes a safety simulation and requires
  the standard Vulkan PASS oracle, self-KillApp ordering, exact-PID removal,
  post-run websrv availability, and no kernel warning beyond the proven single
  raw-ELF `0x4000` baseline.
  The current consumer holds two Vulkan devices and queues concurrently,
  exercises a timeline semaphore, destroys its peer, and allocates/frees
  memory through the survivor. That exact FW 5.500.008 candidate ran once and
  passed: PID 154 printed
  `package-consumer: PASS result=0`, ps5debug-NG proved the exact PID absent,
  and the scoped klog contained only the proven raw-ELF warning. Evidence is
  retained at `20260731T065637Z-package-consumer.log` and
  `20260731T065637Z-package-consumer-target.klog`.
  The SDL/Zink capability track is pinned in
  `analysis/zink-compatibility.md`. Render-pass 2, descriptor update templates,
  timeline semaphores, mutable/incremental WSI, and rectangular line
  rasterization are implemented. Scalar block layout and shader-epilog
  `alphaToOne`, dynamic rendering, custom border color/image swizzle,
  maintenance5, and Vulkan 1.2 are implemented. FW 5.500.008 readback qualifies
  the scalar, alpha, dynamic-rendering, and swizzled-border slices, and the
  strict Mesa GL 2.1 report is zero-gap. The pinned Mesa now also passes the
  SDL native-window EGL/WSI bridge with exact readback, presentation, complete
  native teardown, and immediate relaunch. Mesa's Prospero package keeps the
  EGL `$ORIGIN` RUNPATH and omits Gallium's redundant copy. Preserve the final
  hashes replayed three times on FW 11.60 and twice on FW 5.50.
  `VK_EXT_depth_clip_enable` is closed on FW 5.500.008 and FW 11.600.005.
  Extension enumeration,
  feature query/enablement, rasterization-chain parsing, fail-closed
  validation, and translation through OpenAGC API 45 pass host tests. The
  dedicated negative-Z probe passes twice with exact color, defined depth,
  and stencil oracles; out-of-range unclamped depth is intentionally not
  assigned a Vulkan-defined value. OpenAGC now includes gfx10.3 linear
  attribute clipping in explicit clip-control packets. Two cleanup-guarded
  Mesa/Zink runs pass exact pixels, visible presentation, teardown, immediate
  relaunch, and contain no depth-clip warning. Preserve the pinned ELF/library
  hashes. The identical firmware-neutral depth probe passes twice on each
  endpoint. The same final Mesa/EGL/Zink library set passes both endpoints; do
  not reintroduce a firmware-specific clip-control value.
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
- Migrate from the existing direct low-level integration to OpenAGC's native
  device/resource/pipeline runtime as each complete vertical slice becomes
  available. Do not preserve duplicate PM4, firmware, allocator, transition,
  synchronization, reflection, or VideoOut backends inside the ICD.
- Consume `openagc-psbc` through its versioned compiler/reflection contract and
  native OpenAGC pipeline objects; retain the direct compiler API for tooling
  and migration tests.
- Keep the implementation application-neutral and standards-based. Eden is a demanding compatibility workload and development guide, not a special backend or architectural dependency.
- Preserve FW 5.50 as the current hardware-evidence baseline, then qualify the
  same firmware-neutral native-runtime artifacts on FW 11.60. Other exact
  profiles retain OpenAGC's evidence labels and fail-closed capability policy.

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
- Compile shaders during pipeline creation because vertex layouts,
  tessellation, color exports, multisampling, and geometry/NGG fusion require
  complete pipeline context. Feed that result into native `AgcShader` and
  graphics/compute pipeline objects rather than reinterpreting metadata in the
  ICD.
- Map Vulkan buffers, images, views, samplers, pipelines, command buffers,
  transitions, queues, fences, and presentation to their native OpenAGC
  owners as those APIs stabilize.
- Keep kernel ioctls, PM4 encodings, firmware profiles, GPU registers, heap
  policy, cache-control selection, and VideoOut patching behind OpenAGC APIs.

## Vulkan-PS5 Implementation

### Core driver

- Reuse Vulkan-PS4's API-neutral dispatch, object, allocator, descriptor, render-pass, pipeline-cache, and test patterns, but replace all PS4/GNM structures and commands with `VkPs5*` objects and OpenAGC operations.
- Implement the complete Vulkan 1.1 entrypoint surface, including pNext handling, memory-requirements/bind v2 calls, descriptor update templates, device groups, pipeline caches, queries, and legal unsupported responses for sparse or protected features.
- Report one gfx1013 physical device and an initial universal graphics/compute/transfer queue family. Add a separate async-compute family only after ACE submission is hardware-qualified.
- Derive physical-device features, limits, memory properties, and format support from actual gfx1013/OpenAGC capabilities. Never advertise placeholders solely to satisfy a particular application.
- Support normal uncompressed, depth/stencil, and BC formats first. Report ASTC, ETC, or unqualified formats unsupported until conversion or native support is validated.

### Resources, commands, and synchronization

- Map Vulkan memory types and requirements to OpenAGC heaps and resources.
  Preserve Vulkan mapping, flush/invalidate, aliasing, VMA suballocation,
  allocation-limit, dedicated-allocation, and deterministic-cleanup semantics.
- Represent buffers, images, views, and samplers with native OpenAGC objects
  plus Vulkan-owned mip, layer, aspect, binding, and layout state.
- Record Vulkan commands into native OpenAGC command buffers. Use native copy,
  draw, dispatch, query, transition, and synchronization operations; internal
  compute/graphics shaders remain valid implementation techniques when owned
  through native pipelines.
- Implement render passes, dynamic rendering-equivalent internal behavior, MRT, depth/stencil, clears, resolves, indirect draws, geometry, tessellation, compute, occlusion/timestamp queries, and pipeline statistics where supported.
- Implement Vulkan fences and semaphores through native OpenAGC fences,
  waits/signals, and submission values. Keep host waits bounded and thread-safe.
- Keep sparse resources, transform feedback, descriptor indexing, and advanced
  optional extensions disabled until their complete semantics are implemented.
  Timeline semaphores now have complete host-visible and completion-ordered
  queue semantics, but still await migration to native counters.

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
- Keep FW-specific credential setup, VideoOut registration patches, event
  queues, buffer registration, flip policy, patch restoration, and hardware
  teardown inside OpenAGC. The WSI module owns only Vulkan surface/swapchain
  semantics and translation.
- Present only after the native submission completion requirement is reached,
  using OpenAGC-owned scanout and presentation objects.

## Completed Baseline and Compatibility Milestones

These milestones describe the existing direct-integration baseline. The
native-runtime migration milestones at the top of this file govern future
ordering.

1. Host ICD lifecycle, loader dispatch, physical-device properties, memory, and VVL-clean object tests.
2. Runtime SPIR-V library for VS/PS/CS/GS/tessellation, descriptors, specialization constants, and push constants.
3. FW 5.50 standalone compute, indexed triangle, textured rendering, depth/stencil, MRT, query, geometry, and tessellation samples.
4. General VideoOut-backed Vulkan swapchain sample sustaining at least 1,800 frames with correct acquire/submit/present synchronization and clean teardown.
5. Reusable SDK package consumed by a separate, minimal PS5 homebrew project without source-tree-relative includes.
6. Eden compatibility profile: pass Eden's mandatory extension, feature, limit, format, queue, VMA, shader-pipeline, and presentation checks without application-specific driver behavior.
7. Run `../2048.nro` through `eden-ps5`, followed by broader applications and engines to prevent the implementation from overfitting to Eden.

### Milestone 6 current progress

- `vkCmdClearColorImage` now records a complete public-OpenAGC path for every
  advertised uncompressed color format. A reproducible meta compute pipeline
  covers arbitrary 1/2/4/8/16-byte patterns without an application-specific
  shortcut and batches regular array layers per mip. Exact mip/layer
  layouts, state snapshot/restoration, format packing, VVL RGBA16F coverage,
  and fail-closed compressed/depth cases pass 41/41 normal and sanitizer suites
  plus the Prospero build. Hardware pixels remain pending.
- `vkCmdCopyBuffer` now has deterministic FW 5.50 execution evidence in
  addition to its packet/validation tests. One pinned ELF passed twice with
  144 copied bytes across two nonzero-offset regions, 112 unchanged guards,
  bounded synchronization, cleanup, teardown, and immediate relaunch. Preserve
  this regression while finishing Eden upload and shader-cache integration.
- `vkCmdClearDepthStencilImage` now records single-sample D16, D32, S8,
  D16+S8, and D32+S8 selected mip/layer ranges through exact per-plane OpenAGC
  layouts and the same reproducible meta-compute pipeline. A 70-layer combined
  image batches to one dispatch per selected plane/mip; 48/48 normal and
  sanitizer suites plus the Prospero static/shared build pass. D24 and 4x
  depth/stencil remain fail-closed, and hardware pixels are pending.
- Graphics-meta attachment clearing is host-complete. `vkCmdClearAttachments`
  and render-pass/dynamic-rendering load clears cover partial rectangles,
  advertised color formats, separate/combined depth and stencil aspects, and
  depth-only dynamic rendering through public OpenAGC pipelines and commands.
  Normal and sanitizer suites pass 48/48; one pinned ELF produced identical
  exact color pixels twice on both FW 5.50 and FW 11.60 with teardown and
  immediate relaunch. Depth/stencil pixels remain before full qualification.
- General 2D color blits now use a graphics-meta path with nearest/linear
  filtering, scaled and reversed regions, mip/layer selection, BC or
  uncompressed sources, and uncompressed destinations. Normal and sanitizer
  suites pass 48/48, Prospero static/shared builds are clean, and one pinned
  exact-pixel probe passed twice on both FW 5.50 and FW 11.60. The path now
  also records 3D-to-3D, mixed 2D/3D, and disjoint-subresource 2D self-image
  forms. Their shader regeneration, 48/48 normal and sanitizer suites, and
  Prospero static/shared builds pass. One pinned ELF passed the 64-volume- plus
  16-self-pixel oracle twice on FW 5.50 with bounded waits and clean immediate
  relaunch. Preserve it and defer FW 11.60 replay to the final candidate.
- General 4x-to-1x 2D color resolves now use a reproducible graphics-meta
  shader with strict subresource, usage, layout, format, and bounds validation.
  Normal and sanitizer suites pass 48/48, Prospero static/shared builds are
  clean, and one pinned exact-pixel probe passed twice on both FW 5.50 and FW
  11.60. Preserve those results; qualify subsequent development slices on FW
  5.50 and defer the next identical-byte FW 11.60 replay to the final release
  candidate.
- The graphics-pipeline translator now accepts valid depth-only dynamic
  rendering with zero color formats and zero blend attachments. A D32 pipeline
  exporting only `gl_FragDepth` creates a native OpenAGC graphics pipeline;
  this prerequisite is consumed by the graphics-meta attachment-clear cache.
- The first general format expansion exposes all 14 BC1-BC7 Vulkan formats for
  sampled/filter/transfer use through native OpenAGC layouts and descriptors.
  Five-mip cube/cube-array images, exact per-mip/per-layer layouts, complete
  and nonzero-base mip views, and D24/ASTC/ETC fail-closed behavior pass both
  40/40 normal and ASAN/UBSAN suites plus a clean Prospero library build.
  A deterministic compute gate now samples all fourteen encodings through
  native sampler and combined-image-sampler descriptors and passes twice on
  FW 5.50 with one pinned ELF. Preserve its absent-channel regression and
  reproducible Mesa-codec assets. Exact BC image-copy and cross-mip-copy gates
  now also pass twice on FW 5.50 through the public OpenAGC region-copy path;
  preserve their untouched-mip oracle. The next format slice expands Eden-required
  uncompressed formats without introducing application-specific aliases. The
  first RGBA16/32 signed/unsigned integer
  slice now passes exact clear/readback twice on FW 5.50 with bounded waits and
  immediate relaunch. The following API 48 slice adds fourteen R/RG
  scalar/vector normalized and integer forms and now passes its expanded
  eighteen-format exact clear/readback gate twice on FW 5.50. API 49 then
  adds exact R8/RG8 SNORM, UINT, and SINT mappings and corrects R8/RG8 UNORM
  storage creation; its expanded 26-format, 1,664-pixel gate passes twice on
  FW 5.50. API 50 adds packed RGBA8 SNORM/UINT/SINT, RGB10A2 UINT, and
  BGR10A2 UNORM; its expanded 31-format, 1,984-pixel gate passes twice on FW
  5.50 with one pinned ELF. API 51 adds five renderable packed 16-bit forms
  plus sampled-only R4G4; its expanded 37-format, 2,368-pixel gate passes
  twice on FW 5.50 with one pinned ELF. API 52 adds sampled-only RGB9E5; its
  expanded 38-format, 2,432-pixel gate passes twice on FW 5.50 with one pinned
  ELF. RGB32 remains image-unsupported because gfx10.3 defines it as a
  buffer-only encoding. Qualify shader sampling/storage and scalar/vector attachment
  exports next.
  Integer shader execution and the final identical FW 11.60 replay remain
  before endpoint qualification.
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
- `imageCubeArray` is hardware-qualified with a 12-layer, two-cube sampled
  image. Repeated gates produced exactly 18,432 covered pixels split into
  9,216 red samples from cube 0 and 9,216 green samples from cube 1. Evidence
  is retained in `20260728T170806Z-image-cube-array-run1.log` and
  `20260728T170838Z-image-cube-array-run1.log`.
- `multiViewport` is hardware-qualified for two static viewport/scissor slots
  selected by `gl_ViewportIndex`. Repeated gates produced exactly 9,216 green
  pixels in the left viewport and 9,216 red pixels in the right viewport.
  Evidence is retained in `20260728T173652Z-multi-viewport-run1.log` and
  `20260728T173733Z-multi-viewport-run1.log`.
- The live Eden ICD compatibility profile now reports zero hard gaps:
  `extensions=0 features=0 limits=0 queues=0 total=0`. The separate Eden PS5
  surface/build integration remains application work rather than an ICD
  capability gap.

## Test Plan and Assumptions

- Preserve the existing passing OpenAGC, openagc-psbc, and Vulkan-PS4 baselines.
- Add unit tests for allocation failure, pNext chains, descriptors, specialization constants, render passes, queries, synchronization, pipeline caches, swapchain recreation, and concurrent queue/API use.
- Run Vulkan Validation Layers with zero errors or warnings across standalone graphics, compute, transfer, and WSI applications.
- Add targeted Vulkan CTS/deqp coverage for every advertised Vulkan 1.1 feature and extension. Report a non-conformant conformance version until qualification supports a stronger claim.
- Require deterministic GPU readback and repeated FW 5.50 runs before advertising each hardware feature.
- Eden-specific source changes are limited to PS5 surface creation, build/link integration, and locating the statically linked Vulkan entrypoint. Eden's renderer continues using standard Vulkan.
- Other applications may either link `libvulkan_ps5.a` directly or consume the exported CMake package; they do not depend on Eden.
