# Implementation Status

## Current architecture status (2026-08-02)

The current ICD is a host-qualified native-runtime consumer of OpenAGC and
`openagc-psbc`. Its earlier direct-backend host, loader/VVL, sanitizer,
relocatable-package, and bounded FW 5.50 evidence remains the regression
baseline, but does not qualify the new zero-direct-call candidate.

The target ownership model is host-complete: device, queue, memory, resource,
shader, pipeline, command-buffer, transition, fence, submission, and
presentation paths use public native objects. Capture remains a separate
qualification concern.

Migration status:

- **Continuous qualification klog:** the guarded FW 5.50 runner now has an
  opt-in, fail-closed continuous kernel-log mode. It starts and verifies one
  listener after the pinned cleanup and exact `eboot.bin` absence checks but
  before qualification upload/launch, preserves captured bytes if the console
  disconnects, and stops the listener before exit cleanup so cleanup events do
  not contaminate the target trace. The host regression proves pre-launch
  ordering, post-launch evidence retention, single-listener use, listener
  retirement, and refusal to launch when the listener is unavailable. Snapshot
  mode remains the default for existing qualification wrappers.

- **Eden consecutive descriptor updates:** descriptor writes, copies, and
  update templates now roll across compatible consecutive bindings as Vulkan
  requires, including sparse/zero-count bindings, declaration-order-independent
  flattened slots, and array offsets that advance across bindings. Template
  payloads are copied safely from byte-aligned `offset`/`stride` locations.
  The dedicated regression covers Eden's two one-element storage-buffer
  bindings plus sparse, offset, direct-write, copy, unaligned-data, and
  incompatible type/stage/immutable-sampler cases. All 62 host tests, targeted
  ASan/UBSan, and standalone/source-integrated Prospero builds pass. Exact Eden
  ELF `4eae3b998f9a92664d41b86325a62bc8f9d2186a8c592e471ac180038923e490`
  reached `rasterizer-buffer-cache-runtime`, every later constructor
  checkpoint, and the final `eden-ps5: INIT CHECKPOINT rasterizer` oracle on
  FW 5.500.008 in `20260802T074820Z-swapchain-run1.log`. The bounded run then
  entered a separate audio/thread-resource coredump, so rendering, descriptor
  execution, teardown, and relaunch are not claimed by this slice. See
  `analysis/fw550_eden_descriptor_rollover_20260802.md`.

- **Eden unnormalized samplers:** `vkCreateSampler` now accepts the Vulkan-valid
  unnormalized-coordinate subset, rejects filter, mip, LOD, address,
  anisotropy, and comparison combinations that the Vulkan contract forbids,
  and translates the request through OpenAGC's public
  `AGC_SAMPLER_UNNORMALIZED_COORDINATES_BIT`. Host coverage creates both exact
  linear and nearest samplers used by Eden's `BlitImageHelper` and keeps six
  invalid variants fail-closed. All 61 generic tests pass and the complete
  Prospero build is clean. Exact Eden ELF
  `1d7bdec9a08caf24a23b39fbbac8ec1aefb4ea1a4bd18798e88078d55b18a21f`
  passed both sampler creations on FW 5.500.008 and advanced beyond the former
  `VK_ERROR_FEATURE_NOT_PRESENT` in
  `20260802T061452Z-swapchain-run1`. The next independent gate is a production
  compute pipeline whose 36,864-byte scratch requirement remains fail-closed.
  See `analysis/fw550_unnormalized_sampler_20260802.md`.

- **Bounded idle and teardown:** `vkQueueWaitIdle` and `vkDeviceWaitIdle` now
  serialize against native submission/presentation and wait the current
  OpenAGC fence for at most two seconds. Swapchain and device destruction
  refuse unsafe release when that wait fails. A clean generic build passes
  61/61 tests, the complete Prospero build is clean, and the corrected ICD
  passed two immediate guarded SDL/EGL/Zink runs on FW 5.500.008 with exact
  RGBA `64,128,191,255`, clean PID retirement, and no attributed fatal,
  GPU-reset, or power event. See
  `analysis/fw550_zink_idle_teardown_20260802.md`.

- **CTS discovery:** official `vulkan-cts-1.4.6.1` commit
  `5c8aae22885448d70a2873e94a93b24b49505c32` now loads the host ICD directly
  and completes `dEQP-VK.info.*` without a crash. A missing
  `vkGetDeviceProcAddr` self-query caused the initial null dispatch and is now
  lifecycle-tested. The current result is 18 pass, two `NotSupported`, and
  one fail. OpenAGC commit `cb77512` and the ICD now expose the native 4x color,
  D16, D32, and S8 limits and sampled-image forms, closing the CTS
  `device_properties` failure. The remaining failure is the mandatory Vulkan
  1.2 feature set; this is discovery evidence only and does not change the
  non-conformant `0.0.0.0` report. A fresh Debug build passes all 59 host tests,
  including loader/VVL creation of sampled 4x D32 and S8 images and views.
  Sampled-pixel execution remains pending FW 5.50 qualification.
  The first mandatory-feature follow-up implements core 1.2
  `imagelessFramebuffer`: framebuffer attachment metadata and format lists are
  deep-copied at creation, render-pass begin supplies and validates the live
  views, and command recording binds a command-local attachment set. Positive
  recording plus missing-metadata fail-closed coverage passes in both the
  normal and ASAN/UBSAN 59-test suites and in the Prospero build. The CTS
  mandatory-feature log no longer lists either imageless-framebuffer
  requirement; six unique feature gaps remain.
  Core `uniformBufferStandardLayout` is now host-qualified as well. Dedicated
  and Vulkan-1.2 queries/device requests are enabled, while the pipeline gate
  compiles and reflects a `std430` uniform block beside scalar storage data
  into the native compute pipeline. Normal and ASAN/UBSAN suites remain 59/59,
  the Prospero build is clean, and both duplicated CTS uniform-layout findings
  are gone. Five unique mandatory-feature gaps remain; hardware value readback
  for the standard-layout UBO is pending the guarded FW 5.50 probe.
  Core `separateDepthStencilLayouts` is now host-qualified across dedicated
  and Vulkan-1.2 feature queries/device requests, RenderPass2 attachment and
  reference stencil-layout metadata, dynamic rendering, aspect-aware barriers,
  per-aspect clear/write validation, and final-layout restoration. Packed
  depth/stencil surfaces retain OpenAGC's unified native cache state while the
  Vulkan pipeline independently forbids writes to read-only aspects. The
  normal and ASAN/UBSAN suites pass 59/59, the Prospero build is clean, and the
  official mandatory-feature log no longer lists separate depth/stencil
  layouts. The subgroup slice then closes both extended scalar types and
  dynamic broadcast IDs through an actual Wave32 storage-buffer oracle. Normal
  and ASAN/UBSAN suites pass 60/60, the Prospero build is clean, and the exact
  probe ELF passed twice from cleanup through teardown on FW 5.500.008. See
  `analysis/fw550_subgroup_broadcast_20260802.md`.
  Basic multiview is now closed as well. Dedicated and Vulkan 1.1 queries
  advertise six views with geometry/tessellation multiview disabled; legacy
  RenderPass, RenderPass2, dynamic rendering, pipeline creation, attachment
  layer selection, and per-view draw replay are host-covered. Normal and
  ASAN/UBSAN suites pass 61/61. The exact `0x21` pixel probe passed twice from
  cleanup through readback, self-exit, and immediate relaunch on FW 5.500.008
  with ELF SHA-256
  `00b5361b49d00fdb133ce2e2882618be82cba918df7b83732e268b8a96b01e22`.
  The mandatory-feature log is now down to `borderColorSwizzle` alone. See
  `analysis/fw550_multiview_20260802.md`.
  `borderColorSwizzle` is now closed too. Both extension feature bits are
  advertised and enableable; sampler creation accepts and validates the
  explicit component-mapping chain while the public OpenAGC image-view mapping
  supplies the native sample swizzle. Normal and ASAN/UBSan suites pass 61/61.
  The exact B↔R custom-border probe counted 18,432/18,432 blue pixels twice on
  FW 5.500.008 using ELF SHA-256
  `8e7ed28d20788293fce2a85a5e17072eb557a872efe2f14627620f5d2105012c`.
  Pinned CTS `dEQP-VK.info.*` now reports 19 pass, zero fail, and two expected
  `NotSupported` cases. See
  `analysis/fw550_border_color_swizzle_20260802.md`.
- **Native ownership boundary:** host-complete. The checked TSV has zero rows.
  `vulkan_ps5.native_migration_audit` fails if a direct low-level call returns
  or an inventory entry becomes stale. The advertised
  feature/extension/limit/queue/format snapshot is frozen and its strict
  pinned-Mesa report has zero gaps.
- **Device and resource migration:** the resource slice is active. OpenAGC
  API 28 retains API 26's multiple-logical-device contract, API 27 explicit
  `AgcMemory` plus placed resource binding. It supports exact
  per-device child ownership, matching active AGC defaults versions, and
  last-device shutdown. Every `VkDevice` now owns one native `AgcDevice` and
  native graphics/compute queues. The Vulkan lifecycle gate creates two devices
  concurrently, destroys one, and proves the survivor remains usable.
  `VkDeviceMemory` allocation/mapping/cache operations now use `AgcMemory`,
  and bound `VkBuffer` objects own placed `AgcBuffer` handles. Images now use
  native layout queries and placed `AgcImage`; compatible views are created as
  typed/swizzled `AgcImageView` objects, and every sampler owns a normalized
  `AgcSampler`. Native descriptor writes consume those handles directly.
- **Pipeline migration:** active. Every compiled Vulkan stage now owns an
  `AgcShader`; compute pipelines own `AgcComputePipeline`, and the qualified
  point/line/triangle, geometry, and tessellation graphics forms own
  `AgcGraphicsPipeline`. Shader binary allocation, relocation, cache flush,
  and front/back fusion are now wholly owned by `agcCreateShader`; Vulkan no
  longer retains shader-code allocations or calls a compatibility fusion
  export. OpenAGC API 36 and PSBC API 18 carry an explicit
  alpha-to-one reflection bit and prune
  descriptor sets that have no stage user-SGPR address, while retaining every
  binding in an addressable set. Native ownership now includes polygon modes,
  culling, rasterizer discard, strip/fan primitive restart, depth clamp,
  static/dynamic depth bias and line width, logic operations, and pipeline
  switching. Native command recording consumes these handles directly.
- **Command and synchronization migration:** active. Every Vulkan command
  buffer now owns paired graphics/compute `AgcCommandBuffer` streams because
  Vulkan queue family 0 exposes both workloads while the native objects are
  intentionally queue-typed. Allocate, begin, failed-end rollback, successful
  end, individual reset, pool reset, free, and pool destruction keep both
  streams in lockstep. The graphics stream is now the ordered Vulkan queue
  stream and may carry both graphics and compute pipeline binds; OpenAGC can
  carry their eventual dispatches in that same hardware-proven DCB compute
  carrier. `vkCmdPipelineBarrier`
  translates supported buffer/image barriers into typed native usages,
  ownership, byte ranges, and image subresource ranges. Explicitly transitioned
  buffer copies are also recorded with `agcCmdCopyBuffer`. Resource-less memory
  barriers and queue-family ownership transfers remain fail-closed. Descriptors
  with explicit compatible native resource states bind through
  `agcCmdBindDescriptors`; compute dispatch and baseline graphics attachment,
  descriptor, vertex/index, viewport/scissor, and draw state are mirrored
  natively. Direct, indexed, indirect, tessellation, geometry, and compute
  dispatch recording now require the native path and fail closed when typed
  state is missing. Pipeline switches invalidate and rebind native descriptor
  and vertex state correctly. Every executable graphics stream submits through
  `agcQueueSubmit` with a finite native fence wait. Unsupported command
  mixtures fail closed; the duplicate encoder and fallback submission path are
  deleted. Image-region and
  buffer/image color transfers now record through OpenAGC API 41. General
  uncompressed color and single-sample depth/stencil image clears use public
  OpenAGC compute commands; attachment clears and color blits use public
  OpenAGC graphics-meta paths. Blits cover 2D, 3D, mixed 2D/3D, and disjoint-
  subresource 2D self-image forms. Same-subresource/depth-stencil blits,
  depth/stencil transfer, and unsupported resolve forms fail closed. Indirect draw,
  indexed draw, and dispatch now use typed native argument buffers; the
  superseded Vulkan-side multi-draw encoder has been removed.
- **WSI migration:** host-complete. Swapchain images are dedicated native
  scanout resources retained by `AgcPresentChain`; native queue completions
  publish an `AgcFence` consumed by bounded presentation, and
  acquire exhaustion uses condition-variable wakeup instead of CPU polling.
  The three raw `agcVideoOut*` calls are absent. Linked candidate
  `0b1d87d02a5fbe480cc74890c613752bb55c2e7b5f4e729413314785e5302888`
  passed 1,800 frames and clean teardown on FW 5.500.008.
  The first Eden-owned Release O3 executable now also passes two identical-hash
  600-frame lifecycle runs, including immediate relaunch. During that gate the
  hidden meta-clear alias was fixed to restore its record-time native state,
  making pre-recorded color and depth/stencil clear command buffers safely
  resubmittable. Evidence and the final ELF digest are recorded in
  `analysis/fw550_eden_bootstrap_20260802.md`.
- **Endpoint qualification:** the native lifecycle slice is FW 5.50 qualified
  by the concurrent-device installed-package consumer. Remaining native slices
  and the complete FW 11.60 endpoint qualification are pending.

The API-27 memory/buffer slice is host- and FW 5.500.008-qualified. A clean
generic build passes all 46 tests, ASAN/UBSAN passes all 46, OpenAGC's clean
generic suite passes 19/19 with 17,572 assertions, and the warning-free
Prospero build produced the tested artifacts. The package consumer ELF
`1fd79429140e26884cc492761d8551cf7a2b8769c39066463d9f05f6c9fc5547`
passed native device/memory lifetime. The buffer-copy ELF
`893ca67daae21821466a3bb0df4c75d50076d8e431283d92bd976d69388bbca2`
then copied 144 bytes across two regions with 112 guard bytes intact, proving
the placed-buffer binding offset feeds real GPU execution. Both cleanup-
guarded runs self-terminated and left only the established raw-ELF
`amount=0x4000` warning. Evidence is retained in
`examples/qualification-logs/20260731T082817Z-package-consumer.log` and
`examples/qualification-logs/20260731T082834Z-buffer-copy-run1.log` with their
matching target klogs.

The first API-38 native-copy candidate
`0c1355192de8302bee43172540410a3424480bd379e5aa942d0e523f807e25b5`
kernel-panicked the FW 5.50 console and is retired. The packet and address
audit found that its new native DCB was suballocated beside mutable transfer
resources, whereas the earlier passing carrier used an isolated command
mapping. OpenAGC now allocates kernel-submitted native command storage from
dedicated flexible mappings, and the buffer-copy probe is unbuffered and
always reaches its SystemService-exit wrapper on ordinary failures. Fixed
candidate `35315b83d6731844d825b932e16dad904003b3a0cc6b4114c261b35455ec4d56`
passed the exact two-region/112-guard workload twice after cleanup, left no
process, kept all diagnostic ports reachable, and produced no GPU-fault or
panic signature. See
`analysis/fw550_native_buffer_copy_panic_20260731.md`.

The API-28 image/view/sampler slice is also host- and FW 5.500.008-qualified.
The storage-image candidate
`2030aac81046a6e5b270a9b8e6c2ee2953cf2555d09c753c0c43e8004d10b03f`
wrote all 4,096 deterministic pixels. The depth candidate
`94766f98dfd632b3821df01682dbe0cc78fc724eb93a80c967c6f08db82a5b46`
passed twice with identical `54145/12288/9830` raw-depth counts and 22,118
stencil writes. The cube-array candidate
`2e9cfb91fd3f6c783ccda4f864d5399c80264a7eafdd0b4029c29b0545ec1c84`
produced 9,216 red and 9,216 green samples, while custom-border candidate
`61ef02a082c16723310565ee67e979fb960a2a76a3a78aef6caa8b42ce692bc8`
produced all 18,432 expected blue samples through BR view swizzle. The storage,
cube-array, and border probes used the cleanup/klog guard, self-terminated, and
left only the established raw-ELF `amount=0x4000` warning. Evidence is retained
at UTC timestamps `20260731T084955Z`,
`20260731T085013Z`, `20260731T085153Z`, and `20260731T085214Z`.

The first API-29 shader/pipeline ownership slice is host-, sanitizer-,
Prospero-, and FW 5.500.008-qualified. All 46 normal and ASan/UBSan tests pass;
the pipeline tests prove native shader ownership plus native compute and
qualified graphics pipeline creation. Storage-image candidate
`3198c1a4fe43adb58ddf0de11d223c894c10fba698891df071d05c2cf8ae694a`
created the reflected native compute pipeline and wrote all 4,096 pixels.
Alpha-to-one candidate
`4bcefd5ae07303b4d72d5f6a13b2e3ac921e4be3eecc91ae8d59b88dcfbb153f`
created the reflected native graphics pipeline and produced 18,432 green
pixels. Both cleanup-guarded runs self-terminated with only the established
raw-ELF `amount=0x4000` warning; evidence is retained at UTC timestamps
`20260731T092529Z` and `20260731T092556Z`.

The typed-transition and first native-copy slice passes all 46 generic and all
46 ASan/UBSan tests plus the Prospero build. Command-recording tests assert the
paired native objects progress through Initial, Recording, rollback to Initial,
and Executable states, Vulkan buffer/image barriers produce native transition
journals, and explicitly transitioned two-region copies record through the
native copy API while preserving every legacy command result. A separate
storage-buffer regression proves typed shader-write transition, native
descriptor bind, and a 3x5x7 native dispatch are recorded exactly once.
Descriptor preparation now queries the exact OpenAGC command-buffer range
covered by each buffer descriptor. This preserves valid partial-range
barriers after the coarse whole-buffer mirror becomes undefined; a focused
host regression records a partial 24-byte storage descriptor and FW 5.50
proved that Eden's matching command advances through native dispatch
submission instead of failing during command recording.
OpenAGC's
graphics-DCB compute regression also proves shader-write resource state is
legal on that ordered stream. This is a host-qualified foundation, not a
completed command migration: the API-36 indirect slice removed the first
superseded encoder and reduced the direct-call inventory from 45 to 44.

The first end-to-end native graphics/submission slice is FW 5.500.008-
qualified. A stale Prospero PSBC archive first exposed an unversioned compiler
ABI failure; PSBC now exports `openagcPsbcGetApiVersion`, and Vulkan rejects a
header/runtime mismatch explicitly. OpenAGC API 36 adds the firmware-neutral
`agcCmdSetViewportScissors` array command for all 16 advertised viewports, so
static and dynamic Vulkan viewport/scissor state is materialized in the native
stream instead of inheriting firmware defaults. Candidate
`e424f83e3ef3fb0bb808f7031369305d87cb6bd416504f7e2f3b273577830700`
then passed the cleanup-guarded dynamic-rendering gate with exactly 18,432
green pixels and a clean bounded exit. Evidence is retained in
`examples/qualification-logs/20260731T114855Z-dynamic-rendering-run1.log` and
its matching target klog.

OpenAGC API 36 adds typed `agcCmdDrawIndirect`,
`agcCmdDrawIndexedIndirect`, and `agcCmdDispatchIndirect` commands plus the
dedicated indirect-buffer usage bit. The runtime validates complete 16/20/12-
byte records, final multi-draw bounds, queue state, reflected bindings and
system-SGPR locations, command capacity, and buffer retention. Vulkan now
records indirect graphics and compute through those commands and no longer
calls its legacy multi-draw encoder. Generic command tests cover multi-draw,
indexed multi-draw, dispatch-indirect, typed barriers, native counts, and
fail-closed invalid ranges. The cleanup-guarded FW 5.500.008 indirect-draw
candidate `5e9819ead2bf0fe16218f36a8733af3f8063c3a9702f941dd841b8e068d87fb3`
produced exactly 11,472 green pixels split 5,736/5,736 across the two draws,
with `firstVertex=1`, `firstInstance=1,2`, and `drawID=0,1`. It self-terminated,
left no matching process, and produced only the established raw-ELF
`amount=0x4000` warning. Evidence is retained in
`examples/qualification-logs/20260731T122117Z-indirect-draw-run1.log` and its
matching target klog. The first diagnostic attempt correctly fell back to the
legacy stream because required typed vertex/argument transitions were absent;
the guarded retry added those Vulkan barriers and exercised the native stream.

Eden's Flappy Bird workload exposed the remaining command-side query gap:
`vkCmdCopyQueryPoolResults` rejected the first occlusion resolve after two
successful draws. The driver now records bounded query-copy operations,
prepares the exact destination range as `CopyDestination`, and, after the
already-synchronous native queue submit completes, reduces OpenAGC's opaque
per-RB record into Vulkan 32- or 64-bit results with optional availability.
The result is flushed into host-visible transfer-destination memory before
Vulkan completion is signaled. WAIT, PARTIAL, availability, range, stride,
flag, operation-count, and destination-memory contracts fail closed; device-
local result destinations remain unsupported until a GPU-native reduction
path exists. Mesa RADV's GFX10.3 implementation is the reference for that next
step: it waits on the last enabled RB's availability word and dispatches a
query-reduction shader over the opaque begin/end counters. Command regressions
cover the required zero-count no-op, Eden's WAIT|64-bit copy plus its following
transfer barrier, and rejection of an undersized stride. Host command,
lifecycle, and validation tests pass, and the Prospero static library builds.
The PID 176 Eden replay proved that the query-copy validation itself no longer
rejects, but an unlabelled command immediately after its transfer barrier still
latched `VK_ERROR_FEATURE_NOT_PRESENT`. Fill and occlusion reset/begin/end now
record command labels and print exact native emission failures; successful
query-copy recording is also logged. This diagnostic slice changes no accepted
feature surface.
PID 179 then proved none of those commands were reached: no success/failure
query-copy or fill/query label appeared, while the final last-entry label was
still `vkCmdPipelineBarrier`. The remaining silent barrier branches now print
exact OpenAGC buffer-range or image-subresource state-query failures, including
native result and resource range.
PID 182 produced none of those diagnostics, proving the error is latched by an
unlabelled command after the barrier. Timestamp, direct/indexed indirect-count,
and indexed-draw entrypoints now update the last-command label; the unsupported
timestamp and indirect-count paths also print their exact arguments.

OpenAGC API 37 adds opaque occlusion-query layout/result contracts and typed
query-buffer reset, begin, and end commands. Vulkan query pools now own
`AgcMemory` plus a placed `AgcBuffer`; query recording no longer emits legacy
query snapshots, raw-address writes, or availability packets, and host results
use the bounded native reducer. A query command fails closed if an earlier
operation already made the native stream incomplete, preventing silent loss
through legacy fallback. Generic command coverage records reset/begin/draw/end
on one complete native stream and proves query storage retention. The migration
audit fell from 44 to 40 direct calls. API 38 then moved physical limits,
format masks, sample counts, and heap profiles behind the versioned
`agcGetDeviceProperties` contract. Buffer copies now query effective native
range state, record exact typed transitions, and execute only through
`agcCmdCopyBuffer`; Vulkan no longer computes GPU addresses or packet capacity
for copies. Removing the now-unused legacy capacity helper reduces the audit
to 37. Command tests cover native-only copy followed by legacy fallback and
the reverse ordering, require both to fail finalization, and prove reset clears
the latch before a legacy-only recording. The buffer-copy hardware runner also
requires cleanup-first execution, and native command storage is isolated from
mutable resource allocations. This query slice is host-qualified and
hardware-qualified on FW 5.500.008. The cleanup-guarded candidate
`db041bf9ab4a47eb4fd79f746588410bdcf851a16a680675f8dbcd008298feb5`
returned exactly `samples=18432 green=18432`, self-terminated through
SystemService, left no matching process, and produced only the established
raw-ELF `amount=0x4000` warning. The first native attempt exposed graphics
defaults resetting `DB_COUNT_CONTROL` between begin-query and draw; API 37 now
establishes those defaults atomically before the first query. Evidence is in
`examples/qualification-logs/20260731T125526Z-query-full.log` and its matching
target klog.

The transfer-image migration now consumes OpenAGC API 41.
`vkCmdCopyImage` accepts multiple validated color regions across mips and array
layers, while `vkCmdCopyBufferToImage` and `vkCmdCopyImageToBuffer` preserve
Vulkan byte offsets, row lengths, image heights, subresource layers, signed
offsets, and extents through typed native records. The ICD conservatively
transitions the image and buffer objects to queue-owned copy usage; OpenAGC
then validates subresource layouts, BC block rules, footprints, retention, and
command capacity. The command-recording gate covers a partial image copy and
a strided buffer→image→buffer chain. This paragraph records the original
transfer migration baseline; color and depth/stencil image clears have since
moved to native meta-compute paths, and attachment clears now use a
graphics-meta path. General 2D color blits now use a separate graphics-meta
path, while unsupported blit forms and resolves fail closed instead of
silently succeeding. The
direct-call audit remains 34 because these `agcCmd*` calls replace no audited
low-level symbol.

Attachment clearing is now general Vulkan behavior rather than an Eden
special case. `vkCmdClearAttachments`, render-pass `loadOp=CLEAR`, and dynamic
rendering load clears share lazily cached format/aspect graphics pipelines,
accept partial render rectangles, cover advertised color plus single-sample
D16/D32/S8/combined depth-stencil formats, and restore application state before
subsequent draws. Normal and ASAN/UBSAN suites pass 48/48 and the Prospero
static/shared builds are clean. The guarded FW 5.500.008 candidate passed twice
with exact `green=1152 clear=2944 center=ff00ff00`, clean self-exit, and
immediate relaunch. Its ELF SHA-256 is
`973caa5748468edfc81b2e3d6860eb741c9da52b606a69d0df1fc1c469f13e0e`;
evidence is in the `20260801T091354Z` and
`20260801T091405Z` dynamic-rendering logs. The exact same ELF then passed twice
on FW 11.600.005 with identical pixels, teardown, and immediate relaunch; its
FTP round-trip SHA-256 remained identical. Evidence is in the
`20260801T091924Z` and `20260801T092002Z` logs and
`analysis/fw550_fw1160_attachment_clear_20260801.md`. This cross-firmware
qualifies color attachment/load clears. Depth/stencil attachment pixels remain
unqualified.

General color blits are host-complete through public OpenAGC pipelines,
descriptors, image views, samplers, transitions, and draws. Nearest/linear,
scaled, reversed-axis, mip/layer, BC-source, and uncompressed-source forms are
recorded; uncompressed color destinations are supported. A reproducible
`sampler3D` meta shader and per-slice color bindings cover 3D-to-3D and mixed
2D/3D regions. Self-image 2D blits query recording-time OpenAGC subresource
state, transition disjoint source and destination mip/layer sets concurrently,
then restore their exact prior states. Same-subresource feedback,
depth/stencil, and compressed-destination forms remain fail-closed. Normal and
ASAN/UBSAN suites pass 48/48, shader regeneration is byte-stable, and Prospero
static/shared libraries build clean. The 3D/mixed/self candidate ELF
`de13de9e50c8dc9a0c223f7dd56ea2f0bb8d8b2fb475029d38dae75953c67c47`
passed twice on FW 5.500.008 with exact `volume=64 self=16`, bounded waits,
cleanup-first teardown, and immediate relaunch. The run exposed and then
qualified OpenAGC's single-mip view rebase fix. The earlier 2D candidate ELF
`a2ad727201bea7ad40d1fa85e5bda566d27255a6999d1cf96006e0fcdeecd82d`
passed twice on FW 5.500.008 with exact `pixels=256 guards=144 nearest=2x`,
cleanup-first teardown, and immediate relaunch. The same bytes then passed
twice on FW 11.600.005 with identical pixels and lifecycle behavior; an FTP
download matched the pinned SHA-256 exactly.

General 4x-to-1x 2D color resolves are host- and hardware-complete through a
reproducible graphics-meta shader and public OpenAGC objects. The implementation
validates usage/layout, format/aspect compatibility, mip/layer selection,
offsets, extents, and bounds; it creates per-layer multisample source views,
records a scissored draw into the destination subresource, and restores the
application pipeline. Unsupported depth/stencil, 3D, self, compressed, and
non-4x forms fail closed. Normal and ASan/UBSan suites pass 48/48, shader
regeneration is byte-stable, and Prospero static/shared builds are clean. The
pinned ELF
`acd7aaf9b536f9335d1d69609eaa5a80d366ad040df6e7ce48fe8f6ddfb211de`
passed twice on FW 5.500.008 and twice on FW 11.600.005 with exact
`pixels=1024 color=ff00ff80 samples=4`, clean self-exit, and immediate relaunch.
Evidence is recorded in `analysis/fw550_fw1160_color_resolve_20260801.md`.

The shader-execution migration removes eight more audited symbols. Vulkan no
longer allocates, relocates, flushes, frees, or fuses shader binaries itself;
`AgcShader` owns those bytes through pipeline destruction. Compute dispatch and
all exercised graphics forms—including geometry and tessellation—record only
typed native commands. Command tests now validate native draw/dispatch counts
instead of treating Vulkan's duplicate PM4 stream as the oracle, and cover
pipeline-switch descriptor/vertex rebinding plus explicit tessellation storage
state. This historical checkpoint reduced the direct-call inventory to 26.
The completed host slice then moved descriptor and image-layout ownership,
tessellation resources, command storage, and submission behind public native
APIs; deleted the legacy cursor, frame encoder, raw allocator, and fallback;
and reduced the mechanically checked inventory to **zero**. Normal and
sanitizer host suites pass 46/46, and the Prospero target set builds cleanly.
The focused zero-call candidate passed the FW 5.500.008 custom-border oracle
with `covered=18432 blue=18432 swizzle=BR`, clean self-exit, and only the
established `amount=0x4000` warning; evidence is
`examples/qualification-logs/20260731T155934Z-custom-border-color-run1.log`.
The broader FW 5.50 candidate sequence remains pending.

The first FW 5.500.008 native-only custom-border attempt failed safely at
submission and exposed that OpenAGC encoded the sampler index without owning
or programming its table. OpenAGC API 42 now owns the 4,096-entry table and
programs its base on the native graphics stream; Vulkan passes the exact
128-bit custom value through `AgcSamplerDesc` v3. Rebuilt candidate
`53b5f333d704220c91d291d2254534676a7bf6888e0112a30e047109d3d8e025`
then produced `covered=18432 blue=18432 swizzle=BR`, self-exited, left no
matching process, and retained only the established raw-ELF `amount=0x4000`
warning. Evidence is in
`examples/qualification-logs/20260731T153447Z-custom-border-color-run1.log`.

OpenAGC API 39 adds typed buffer update and fill commands with complete-range
CopyDestination validation, atomic command-capacity preflight, embedded data,
and destination retention. Vulkan `vkCmdUpdateBuffer` and `vkCmdFillBuffer`
now validate their standard limits and record exclusively through those native
commands; they are no longer silent stubs. This command-completeness slice does
not add or remove a low-level symbol, so the audit remains at 37 pending
deletion of the shared legacy command/submission path.

OpenAGC API 40 extends the native scanout contract to the ICD's frozen
BGRA8-SRGB surface format. Vulkan WSI now propagates dedicated-allocation
intent into `AgcMemory`, initializes all three swapchain images to native
VideoOut scanout state on the ordered graphics queue, and presents with the
queue's causally published completion fence. The focused WSI and migration-
audit gates pass, reducing the checked direct-call inventory from 37 to 34.
The first cleanup-guarded FW 5.50 candidate failed safely before submission
because Vulkan `oldLayout=UNDEFINED` was encoded literally after native WSI
had already initialized the image to scanout. Undefined-layout barriers now
discard from the tracked current native state and elide the already-scanout
transition; the focused WSI test reproduces that exact command recording.
Rebuilt ELF
`0b1d87d02a5fbe480cc74890c613752bb55c2e7b5f4e729413314785e5302888`
then passed 1,800/1,800 acquire/submit/present frames on FW 5.500.008, complete
Vulkan teardown, self-exit, exact-PID absence, and a clean scoped kernel log
apart from the established raw-ELF `amount=0x4000` warning. Evidence:
`examples/qualification-logs/20260731T142327Z-swapchain-run1.log` and its
matching target klog.

The current native `vkCmdCopyBuffer` path is also independently qualified on
FW 5.500.008. One pinned ELF copied 64- and 80-byte regions at nonzero offsets,
verified all 144 copied bytes and 112 untouched guards, and passed twice with
cleanup-first execution, bounded fence wait, teardown, exact-PID absence, and
immediate relaunch. The ELF SHA-256 is
`8429fb631a76db85b5f2f54e99952c1eafd96670672758c3eb1ba4790789b8e8`;
see `analysis/fw550_buffer_copy_20260802.md`.

The 2026-08-01 current-commit WSI replay initially failed before device and
swapchain creation because the sample requested one of the two advertised
surface formats and incorrectly treated Vulkan's required `VK_INCOMPLETE`
enumeration result as fatal. The sample now uses the standard count-then-fill
enumeration pattern already covered by the direct WSI test. Rebuilt artifact
`becaf8bccafb29f9ce155de486698162f35fd5eb02fcbf447870a971a45aa84f`
then completed all 1,800 acquire/submit/present frames, full teardown,
self-exit, exact-PID absence, and the scoped kernel-log gate with only the
established raw-ELF `amount=0x4000` warning. Evidence is in
`examples/qualification-logs/20260801T131758Z-swapchain-run1.log` and its
matching target klog.

The WSI contract was replayed again after the Eden format and attachment
qualification series at Vulkan-PS5 revision `e78b64eaf8`. ELF
`049b5ad984083518dfe8d69d13c45db63dc995e82d3ad705401e7b9458a08d5c`
passed all 1,800 frames, bounded waits, teardown, exact-PID absence, scoped
kernel-log validation, and immediate websrv recovery on FW 5.500.008. A
post-run FTP download reproduced the local SHA-256 exactly. The guarded runner
now requires pinned local and remote hashes for both the workload and cleanup
ELFs before launch; its fail-closed test covers a wrong workload hash. See
`analysis/fw550_swapchain_regression_20260802.md`.

The completed Eden profile and all hardware-qualified Vulkan features remain
supported by the current implementation. Migration must not silently drop an
advertised feature. Conversely, a native OpenAGC capability is not advertised
through Vulkan until Vulkan semantics, validation, and exact-firmware gates
also pass. `PLAN.md` is authoritative for migration order; the dated sections
below are the implementation and qualification ledger.

The Eden `612409c7ba` format audit is now revision-frozen rather than
approximate: 112 guest rows map to 109 unique Vulkan formats. The committed
inventory classifies 68 unique direct image formats through API 52, no genuine
uncompressed image gaps, 28 ASTC plus 10 ETC2/EAC transcode-required forms,
two fail-closed D24 forms, and RGB32 as deliberately buffer-only. The four RGBA integer
formats expose sampled, storage, color-attachment, and transfer use without
unsupported filtering, blit, or texel-buffer claims. Exact format queries,
image/view creation, signed and
unsigned clear packing, the frozen capability snapshot, both 50/50 host
suites, and the Prospero build pass. A dedicated linear-image gate then
created native views for all four formats, cleared and read back all 256 exact
signed/unsigned pixels, and passed twice back-to-back on FW 5.500.008 with one
two-second fence bound, normal teardown, exact-PID removal, immediate relaunch,
and only the accepted raw-ELF `0x4000` warning. The identical probe ELF is
`85ed7b3d39f36573cf64f34498bcc4bdaa472c3cc3a8c63c6c3f1789b8c96fff`;
evidence is recorded in `analysis/fw550_integer_formats_20260801.md`. Storage
image writes are now covered by the 30-format execution gate below. Nearest
sampled-image execution is covered by pinned ELF
`e4e5dc18fd53933a7d810e5f123a30e5c2249e9c75e24b922f9d8169bc38ad19`,
which passed all 38 formats and 152 exact result components twice on FW
5.500.008 with bounded waits and clean teardown. The unsafe, Mesa-inferred
`CS_PARTIAL_FLUSH` event experiment is retired after a user-observed kernel
panic; it is not part of the runtime. See
`analysis/fw550_format_sampling_20260802.md`.

Scalar/vector attachment execution is now FW 5.500.008-qualified. The ICD
supplies each dynamic-rendering attachment's required SPI export format to
PSBC, preserves integer component classes through late NIR export lowering,
and queries the exact command-buffer image subresource state for the final
host-read transition. The first bounded run failed safely at RGB10A2 UINT and
exposed OpenAGC's `COLOR_10_10_10_2`/`COLOR_2_10_10_10` target mismatch. With
the corrected `0x09` CB encoding, final ELF
`e4e2f72bc4356cc8b5a08d3a8f6968069d13456e3ee7b273b98991134fbf3bb5`
passed all 36 formats and 36 bit-exact pixels twice (`20260801T190225Z`,
`20260801T190248Z`) with two-second fence bounds, normal teardown, exact-PID
absence, immediate relaunch, and only the accepted raw-ELF `0x4000` warning.
Normal and ASan/UBSan suites pass 59/59, including the zero-direct-call native
migration audit. See `analysis/fw550_format_attachments_20260802.md`; the
identical-byte FW 11.60 replay remains deferred to the final endpoint gate.

The next fourteen Eden formats are host/Prospero qualified through OpenAGC
API 48: R/RG 16-bit UNORM, SNORM, UINT, and SINT; RGBA16 UNORM and SNORM; and
R/RG 32-bit UINT and SINT. Vulkan exposes sampled, storage, attachment,
transfer, filtering, and blit bits only where the numeric class permits them,
keeps all fourteen texel-buffer claims false, creates optimal images and
native views, and packs exact normalized/integer clears. OpenAGC validates
R32/RG32 against their real `32_R`/`32_GR` exports instead of an invalid ABGR
assumption. The frozen capability snapshot, both 51/51 normal and sanitizer
suites, and the complete Prospero build pass. The expanded bounded probe then
created all eighteen new image/view forms and passed 1,152 exact clear/readback
pixels twice back-to-back on FW 5.500.008 with teardown, exact-PID removal, and
immediate relaunch. Its SHA-256 is
`ec8527214b1681525ec7eb92ab5c24f4f05dfa1fbe027c1f9781415f0853a827`;
see `analysis/fw550_scalar_vector_formats_20260801.md`. Storage-image execution
is now qualified by the 30-format gate below and sampled-image execution by
the 38-format gate. Attachment exports and FW 11.60 remain pending.

OpenAGC API 49 and Vulkan now directly map R8/RG8 SNORM, UINT, and SINT with
exact native layouts, sampled descriptors, color-target classes, clear
packing, and storage-image creation. R8/RG8 UNORM storage-image creation was
also corrected to match its already committed Eden classification. The frozen
capability baseline, 55/55 normal tests, 55/55 ASAN/UBSAN tests, and full
Prospero build pass. Pinned ELF
`73783127fb59f8a31e6c5cdc7500d5d45da5b78ce3d694cd767d92dd72b9f3ed`
passed twice on FW 5.500.008 with all 26 formats and 1,664 pixels bit-exact,
bounded waits, cleanup-first execution, clean teardown, PID absence, and
immediate relaunch. See `analysis/fw550_r8_rg8_formats_20260801.md`; storage
execution is now qualified while sampled-image and attachment-export gates
remain pending.

OpenAGC API 50 and Vulkan now directly map packed RGBA8 SNORM/UINT/SINT,
RGB10A2 UINT, and BGR10A2 UNORM images with exact component selection,
color-target classes, clear packing, and numeric-class feature masks. The BGR
UNORM form deliberately omits storage-image support. The frozen capability
baseline, 55/55 normal tests, 55/55 ASAN/UBSAN tests, and complete Prospero
library/example build pass. Pinned ELF
`07384ba86e1db7b69b3994be320fe4a35fc05db6eec1773d761aa2a9a66602b8`
passed twice on FW 5.500.008 with all 31 formats and 1,984 pixels bit-exact,
bounded waits, cleanup-first execution, teardown, PID absence, immediate
relaunch, and no panic. See `analysis/fw550_packed_formats_20260801.md`;
storage-image execution is now qualified; sampled-image execution, attachment
exports, and FW 11.60 remain pending.

OpenAGC API 51 and Vulkan now directly map R5G6B5, B5G6R5, R5G5B5A1,
A1R5G5B5, A4B4G4R4, and R4G4 UNORM images. Exact format properties preserve
the sampled/filter/transfer/blit/attachment boundary without storage or
texel-buffer claims; R4G4 remains sampled-only because gfx10.3 has no matching
CB format. Both 55/55 host suites and the complete Prospero build pass. Pinned
ELF `0a5b5f4a89d2a2b52dd54e935d8c7385215197b22fdbad63dd0fd5287b12f07d`
passed twice on FW 5.500.008 with all 37 formats and 2,368 pixels bit-exact,
bounded waits, cleanup-first execution, teardown, PID absence, immediate
relaunch, and no panic. See `analysis/fw550_packed16_formats_20260801.md`.

OpenAGC API 52 and Vulkan now directly map RGB9E5 shared-exponent images for
sampling, filtering, transfers, and source blits. Color-attachment and storage
features remain disabled while the gfx10.3 RB+ partial-mask erratum lacks a
qualified runtime workaround. Clean normal and ASAN/UBSAN suites pass 57/57,
and the complete Prospero library/example build passes. Pinned ELF
`29a2cd389a895e29275a3c527a6668c217dc53cd897748f02625ad3dc34b60d3`
passed twice on FW 5.500.008 with all 38 formats and 2,432 pixels bit-exact,
bounded waits, cleanup-first execution, teardown, PID absence, immediate
relaunch, and no panic, reset, timeout, or GPU fault in target-attributed
logs. See `analysis/fw550_rgb9e5_format_20260801.md`.

All 30 storage-capable formats in the refreshed scalar/vector inventory now
have direct shader execution evidence. Three formatless compute shaders cover
float, unsigned, and signed image classes; each target uses a distinct
descriptor set and reflected 16-byte push constant, and exact linear-layout
readback checks all 480 pixels. The first FW 5.50 run exposed the clear
packer's incorrect SNORM -1 endpoint. Vulkan's fixed-point rule reserves the
most-negative encoding, so `native_snorm8`/`native_snorm16` now emit
-127/-32767 and their clear tests/oracles match hardware shader conversion.
Clean normal and ASAN/UBSAN suites pass 57/57, the clean Prospero build passes,
and pinned ELF
`8b15a1053c9f7bdfb57f419f10f0b761563009a8d0056bb3c07d8b9e24d379b2`
passed twice on FW 5.500.008 with bounded waits, teardown, PID absence, and
immediate relaunch. See `analysis/fw550_format_storage_20260802.md`.

The Eden extended-image follow-up is qualified on FW 5.500.008. Probe
`215169ea600dac81901ab423d36d342ee7d9df98537e5119b0fb591c1e09f96e`
first repeated the 30-format exact-bit matrix, then created/bound Eden's exact
480x480 optimal A8B8G8R8 mutable extended-usage image, issued a compute write
through its listed SNORM storage view, completed the fence, tore down, and
left PID/global exact-process absence. The accepted log is
`examples/qualification-logs/extended-storage/20260803T132208Z-swapchain-run1.log`.

The BC slice now has a real shader-execution gate rather than format-query-only
coverage. Compute descriptor preparation realizes Vulkan samplers, combined
image samplers, sampled images, storage images, and input attachments through
public OpenAGC objects. Committed Mesa-codec assets provide one deterministic
4x4 block for every BC1-BC7 UNORM/SNORM/SRGB/UFLOAT/SFLOAT format. A smoke run
exposed BC5's missing-channel selector bug in OpenAGC; after the firmware-
neutral selector fix, the full fourteen-format compute-sampling ELF passed
twice back-to-back on FW 5.500.008. The pinned ELF SHA-256 is
`601d0d2694c819e48140b429bb9e16b473ea91b5c9ad9eaac69bb8ae8624b639`;
see `analysis/fw550_bc_sampling_20260801.md`. These runs were bounded, tore
down normally, left no matching process, and immediately relaunched. They do
not qualify filtered or cube sampling, or the final FW 11.60 replay.

BC image copies and cross-mip copies are now independently qualified through
the typed `vkCmdCopyImage` to `agcCmdCopyImageRegions` path. One pinned ELF
copies two regions for every advertised BC format: source mip 0 to destination
mip 1 and source mip 1 to destination mip 2. It checks all 28 regions byte for
byte plus an untouched destination mip. SHA-256
`0c97df8a72c21577c543dd64649d3f3fc5e0e7f74190adf4ab2ba235bd3b74d4`
passed twice back-to-back on FW 5.500.008 with cleanup-first lifecycle gates,
bounded waits, exact-PID removal, immediate relaunch, and only the accepted
raw-loader `0x4000` warning; see `analysis/fw550_bc_copy_20260801.md`. The
complete normal and ASAN/UBSAN suites now pass 55/55.

## SDL/Zink capability slice (2026-07-31)

The pinned Mesa GL 2.1 Zink reporter and its exact remaining contract are
recorded in `analysis/zink-compatibility.md`. Vulkan-PS5 now enumerates and
implements maintenance1, render-pass 2, descriptor update templates, timeline
semaphores, image format lists, mutable swapchains, incremental present, and
rectangular line rasterization, dynamic rendering, custom border colors and
image-view border swizzle, maintenance5, and Vulkan 1.2. Scalar block layout
passes a real compiler pipeline, and `alphaToOne` changes the generated gfx1013
pixel epilog through PSBC API 16. The pinned strict report now passes with
`api=0 extensions=0 features=0 total=0`.

FW 5.500.008 now hardware-qualifies both additions through cleanup-guarded,
finite-wait readback probes. Scalar layout used a 12-byte `vec3` array stride,
wrote its result at byte 24, and preserved the std430-offset guard; its ELF
SHA-256 is `c46f0cf46128c3460b44255b601636cbd591a1cbe513b94e6454f265783045fa`.
Alpha-to-one converted a shader alpha of 0.25 into 18,432 opaque-green pixels;
its ELF SHA-256 is
`7c07a902fd7a50cc158a2d5430100b3c5df5c3c25e3127b1805b9bee7b74143d`.
Both exact PIDs self-terminated, websrv remained reachable, and each scoped
klog contained only the established raw-ELF `amount=0x4000` warning. Evidence
is retained in `examples/qualification-logs/20260731T072748Z-scalar-block-layout-run1.log`,
`20260731T072748Z-scalar-block-layout-run1-target.klog`,
`20260731T072824Z-alpha-to-one-run1.log`, and
`20260731T072824Z-alpha-to-one-run1-target.klog`.

Dynamic rendering produced exactly 18,432 green pixels on FW 5.500.008 from a
render-pass-less pipeline; its ELF SHA-256 is
`0bb13e7f34a45bf0b8a5cc06779cabc687e85bbb1b4ca6e358fb0c5cf58c26bd`.
The final custom-border candidate sampled an out-of-range opaque-red table
entry through an R/B-swizzled image view and returned exactly 18,432 blue
pixels while using maintenance5 `vkCmdBindIndexBuffer2KHR`. Its ELF SHA-256 is
`dbf161fbc8e77287cbdfb0254170c8275c9831ef4208e67b215cc38e7a7265d2`.
Evidence is retained at timestamps `20260731T074115Z` and `20260731T075944Z`;
both runs self-terminated with only the established raw-ELF warning.

After rebuilding a stale Prospero PSBC archive against compiler API v15, the
FW 5.500.008 rectangular-line gate passed exact width and center-color oracles,
self-terminated, and left only the established raw-ELF `amount=0x4000`
warning. Evidence is retained at `20260731T065623Z-wide-lines-run1.log` and
`20260731T065623Z-wide-lines-run1-target.klog`; the ELF SHA-256 is
`096ddfbc00ed6eab5c4f761ce6b506291736e97e800a8ed5703328d829acc2d5`.
This closes the pinned capability gate, but does not yet qualify Zink or SDL
accelerated OpenGL; the pinned Mesa EGL/WSI execution gate remains.

### SDL/Zink execution integration (2026-08-01)

The FW 5.500.008 pbuffer probe now reaches Mesa Zink screen creation, EGL
context creation, and the expected Vulkan 1.2 renderer string. Vulkan push
constants are command-buffer state: writes made before or after pipeline bind
are shadowed per stage and replayed against the owning stage's reflected native
range before a draw or dispatch. OpenAGC also gives every stage separate arena
backing, so overlapping vertex and pixel ranges may retain different values.
Buffer barriers query the exact byte-range usage and owner from the native
command buffer instead of relying on the coarse whole-buffer mirror. Same-state
buffer/image dependencies still emit an OpenAGC transition so visibility is
not discarded. Generic regressions cover stage-distinct values and a partial
range followed by a zero-source barrier.

The hardware trace exposed two additional ICD gaps. Zink uses dynamic-rendering
`loadOp=CLEAR` and a global color-write-to-transfer-read dependency for the
pbuffer readback; both now have host-tested native recording paths for the
RGBA8/BGRA8 formats used by the gate. Mesa's readback image also requires the
mutable `R8G8B8A8`/`A8B8G8R8_PACK32` UNORM/SRGB compatibility class, which is
now advertised and accepted. The 46-test generic suite remains the gate.

The clean-reboot investigation exposed and closed three final defects. The
native runtime had inverted the ES/LS launch program and GS/HS continuation
program, BGRA8-UNORM sampled views lacked the already-qualified RGBA SQ encoding
plus R/B swap, and the aliased dynamic-rendering clear buffer lacked an
explicit copy-write release before image consumption. Vulkan object wrappers
may also be destroyed while completed executable command buffers retain their
native objects, so the ICD now defers buffers, images, views, samplers,
pipelines, shaders, and memory until command recycling and collects them in
dependency order. A generic regression proves that deferred native count
returns to zero after pool reset.

The diagnostic-free final candidate passes all 47 generic tests and all 47
ASan/UBSan tests, including the strict zero-gap Zink profile and package
relocation gate. The Prospero shared-ICD verifier reports `204` exports and
only `RELATIVE`, `GLOB_DAT`, and `JUMP_SLOT` relocations. Mesa commit `6dbc12f`
keeps `$ORIGIN` on EGL but removes Gallium's redundant identical RUNPATH. The
exact candidate then passed three consecutive FW 11.600.005 runs at
`20260801T060248Z`, `20260801T060313Z`, and `20260801T060341Z`, followed by
two FW 5.500.008 replays at `20260801T060410Z` and `20260801T060435Z`. Every
run returned renderer `zink Vulkan 1.2`, exact RGBA `64,128,191,255`, visible
presentation, no native lifetime error, raw-klog PID-attributed teardown, and
immediate relaunch without reboot. Final tested hashes:

- `testps5zink`: `95da10acf89da3e35865890874034b8bffef1c563417309a0e4bb98404540ad9`
- `libvulkan_ps5.so`: `eacc4cf3dd1c15983e9f78482d65b14250d073a161c0b02433087eaeb5b6d271`
- `libEGL.so.1.0.0`: `0d2922b30b3dbbe25f060331043bb4a4732272d0813023568381306528913fc1`
- `libgallium-26.3.0-devel.so`: `75f3c3fcd229387557d4649af9eee293ac485feeaea1904e48649370565b6b5f`

The subsequent `VK_EXT_depth_clip_enable` closure is hardware-qualified on FW
5.500.008. The ICD enumerates the extension, reports and accepts
`VkPhysicalDeviceDepthClipEnableFeaturesEXT`, rejects use without feature
enablement or with nonzero reserved flags, parses the static rasterization
state, and maps its independent Boolean to OpenAGC API 45. Pipeline inspection
proves both explicit disable and explicit enable while depth clamp is active.
A cleanup-guarded depth probe whose near triangle is outside the Vulkan Z
frustum now passes with the same firmware-neutral OpenAGC clip state twice on
FW 11.600.005 (`20260801T055428Z`, `20260801T055445Z`) and twice on FW
5.500.008 (`20260801T055514Z`, `20260801T055537Z`). Every replay returned exact
`green=12288 red=9830 raw=54145/0/9830 stencil=22118`; the identical ELF
SHA-256 is
`eb3ce7775f5aefe5dd232b44b9e85b781da9a78270e268fba3e3d12a06341cc2`.
The first Zink replay exposed a missing gfx10.3 `DX_LINEAR_ATTR_CLIP_ENA` bit
in OpenAGC's explicit clip-control register. OpenAGC commits `1d42288` and
`34cbceb` corrected the named encoding and proved one firmware-neutral gfx1013
value; the final Zink logs contain no `VK_EXT_depth_clip_enable` warning. This
closes both the FW 11.60 depth-clip replay and the full SDL/EGL/Zink endpoint
gate with identical application and library bytes.

### SDL/Zink regression replay and geometry varying fix (2026-08-02)

The latest OpenAGC API 53, openagc-psbc library API 20, Vulkan-PS5, Mesa/Zink,
and SDL heads were replayed together on FW 5.500.008. Strict OpenAGC attachment
validation exposed two ICD defects: mutable SRGB scanout images need the
compatible UNORM view format forwarded to native color-target binding, and
unused dynamic blend constants must not be forwarded to a pipeline that does
not declare them. Core Vulkan depth clipping is also explicit when the
depth-clip extension structure is absent; omission means enabled, not an
unspecified native default.

The replay then isolated an openagc-psbc compiler defect. Mesa's RGBA readback
conversion used a fused NGG geometry pipeline and returned transparent black
because deterministic fragment-parameter export assignment covered vertex and
tessellation-evaluation stages but omitted geometry. The GS therefore exported
position but not its user varying. Compiler coverage now requires the GS-back
record to own parameter export zero, and Vulkan again advertises
`geometryShader = VK_TRUE`.

With that truthful capability set, all 61 generic tests pass. Three consecutive
cleanup-guarded FW 5.500.008 SDL/EGL/Zink runs returned exact RGBA
`64,128,191,255`, visibly presented, completed native teardown, and relaunched
immediately (`20260802T025708Z`, `20260802T025734Z`, and
`20260802T030426Z`). The tested ICD SHA-256
is `a06711072c85a72a9b8f1424815b71b7d137cd709b56768dcf4b8aff80decf6b`.

With geometry re-enabled, two further cleanup-guarded SDL/EGL/Zink runs passed
exact RGBA `64,128,191,255`, teardown, and immediate relaunch
(`20260802T032613Z` and `20260802T032634Z`). A strengthened standalone sample
now carries green through a GS-to-FS user varying; ELF SHA-256
`abff21e51e69179ccea2feef874d0920c2229384517a3d0d1ab375a9da89c425`
produced exactly 4,608 green pixels twice
(`20260802T033240Z-geometry-run1.log` and `run2`). The final diagnostic-free
ICD SHA-256 is
`5c71a2983769129be33bf82ffaa4d1b13d37a1aae9cdce6ab9512ad627790b25`;
its SDL application printed the exact pixel PASS, but the console became
unreachable before the runner could qualify teardown, so that exact-byte
teardown replay remains pending.

The follow-up API-53 v1 reserved-field compatibility fix changed the linked
bytes without changing the Vulkan path. The rebuilt ICD SHA-256
`6ac5a0454bb9ec380d86287441cc93af11bd41c2cfa2c57a91b1abee6d4dbc7f`
then passed two more consecutive cleanup-guarded FW 5.500.008 replays with the
same exact RGBA, visible presentation, PID-attributed teardown, and immediate
relaunch (`20260802T031213Z` and `20260802T031241Z`). This is the current
development candidate; FW 11.60 replay remains a final-gate requirement.

## Milestone 6: multiple viewports (2026-07-29)

`multiViewport` is advertised and accepted through legacy and Features2
paths, with the gfx1013-qualified limit of 16. Graphics pipelines accept
matching static or dynamic viewport/scissor arrays, command buffers track
dynamic slots atomically, and baseline, geometry, and tessellation draws emit
OpenAGC's typed per-slot viewport state after shader binds. Host regressions
cover two-slot static and dynamic pipelines, missing dynamic state, limits,
exact slot-one registers, feature requests, and VVL-clean use.

Normal and ASAN/UBSAN suites pass 39/39, and the full Prospero build links the
probe with `-lunwind -lc++abi -lc++ -lm`. The first hardware diagnostic
rendered the expected 18,432 covered pixels but used an unqualified geometry
to-fragment color varying, so its color oracle failed without a GPU fault or
console freeze. The final application-neutral probe instead distinguishes the
two viewport regions from `gl_FragCoord` while the geometry shader selects
slots with `gl_ViewportIndex`. Two bounded FW 5.500.008 runs then produced
exactly `green=9216 red=9216 viewports=2`, self-exited, and left the console
responsive. Evidence is in `20260728T173652Z-multi-viewport-run1.log` and
`20260728T173733Z-multi-viewport-run1.log`; only the known single
`amount=0x4000` warning remained. The ELF SHA-256 is
`72adfa0db768fc9ccf113b1c46eecc9b6467edba9d8a9ea62a554acf1a5351d7`.
The Eden ICD profile now reports zero hard startup gaps.

## Milestone 6: cube image arrays (2026-07-29)

`imageCubeArray` is advertised and accepted through legacy and Features2
paths. Cube-compatible RGBA8/BGRA8 images validate square 2D geometry and face
layer counts; 2D, 2D-array, cube, and cube-array views retain resolved layer
ranges. Sampled descriptors lower those views through OpenAGC's typed gfx1013
`CUBE`/`2D_ARRAY`, `BASE_ARRAY`, and last-layer state, including 2D views of
individual array layers. Both linear and optimal Vulkan tiling are backed by
the qualified internal linear layout for this one-mip path.

Normal and ASAN/UBSAN suites pass 39/39. Coverage includes image-format
properties, allocation and subresource pitches, all supported view types,
invalid face counts, descriptor updates, a runtime `samplerCubeArray`
pipeline, and VVL-clean optimal cube-array resources. Two bounded FW 5.50
runs produced the exact 18,432-pixel oracle split into 9,216 red pixels from
cube 0 and 9,216 green pixels from cube 1, then self-exited. Evidence is in
`20260728T170806Z-image-cube-array-run1.log` and
`20260728T170838Z-image-cube-array-run1.log`; only the known single
`amount=0x4000` warning remained. The Eden profile now has one feature gap:
`multiViewport`.

## Milestone 6: sample-rate shading (2026-07-28)

`sampleRateShading` is advertised and accepted through legacy and Features2
paths. Vulkan exposes 4x optimal-tiled RGBA8 color attachments, uses OpenAGC's
typed `64KB_R_X` layout and post-shader-bind sample state, forwards sample
masks, and rounds `ceil(rasterizationSamples * minSampleShading)` to the
supported 1/2/4 gfx1013 iteration rates. A fragment shader with sample
builtins forces the full rasterization sample count through openagc-psbc API
version 12 metadata.

Normal and ASAN/UBSAN suites pass 38/38, including a VVL-clean 4x
image/render-pass fixture and sample-builtin pipeline compilation. Repeated FW
5.50 full-rate gates produced exact sample-ID counts
`18,336/18,528/18,432/18,432` and 73,728 total invocations. Repeated partial
`minSampleShading=0.5` gates produced exactly 36,960 invocations with four
untouched guard words. Every gate queried and requested the public feature,
self-exited, and left only the known single `amount=0x4000` warning.

The 2026-08-01 current-commit regression replay also covered pipeline-forced
partial shading when the fragment shader does not read a sample builtin.
openagc-psbc commit `6161ff7` now reflects the effective compiled iteration
count, including that forced rate. The full-rate artifact
`b13ed0d108709c7e05504cb0dba4c3f93b72d1d26bbe6adc83e452ac81187ac6`
passed with the same sample-count multiset (the first two sample IDs were
reported in the opposite compiler-dependent order), and the partial-rate
artifact `05bf567935e60ef56988474840b19cfcc3c1191df62bb293ad88a4c7624d3cda`
passed with exactly 36,960 invocations and zeroed guards. Both exited cleanly;
evidence is in `20260801T125913Z-sample-rate-shading-run1.log` and
`20260801T130420Z-partial-sample-rate-shading-run1.log`. The sample probe now
also releases its result buffer for every fragment-result variant, preventing
the native child-object leak seen during the first replay.

## Milestone 6: robust buffer access (2026-07-28)

`robustBufferAccess` is advertised and accepted through legacy and Features2
paths. Uniform and storage buffers use byte-bounded raw gfx1013 descriptors;
robust vertex pipelines use openagc-psbc's per-attribute lowering and densely
packed descriptors with complete-record bounds. OpenAGC commit `3928be5`
permits the required zero-record structured descriptor, and openagc-psbc commit
`8369fea` exposes the robust pipeline context and descriptor-mode metadata.

Normal and ASAN/UBSAN suites pass 38/38, including loader/VVL coverage, and the
Prospero probes link with `-lunwind -lc++abi -lc++ -lm`. Two bounded FW 5.50
compute runs proved OOB SSBO reads return zero and OOB stores are discarded;
two sparse-binding vertex runs proved an OOB `vec2` attribute is zero through
an exact 18,432-blue-pixel oracle. All four runs exited cleanly with only the
known `amount=0x4000` baseline warning. Evidence is retained in
`20260728T161337Z-robust-buffer-run1.log`,
`20260728T161845Z-robust-buffer-run1.log`,
`20260728T161642Z-robust-vertex-run1.log`, and
`20260728T161917Z-robust-vertex-run1.log`. The Eden compatibility profile now
has three feature gaps.

## Milestone 6: dual-source blending (2026-07-28)

`dualSrcBlend` is advertised and accepted through legacy and Features2 paths.
Pipeline creation maps all SRC1 color/alpha factors, restricts them to MRT0,
and supplies complete dual-source context to `openagc-psbc`. The compiler uses
native gfx1013 MRT0/MRT1 32-bit ABGR exports and Oberon's DB dual-export state;
OpenAGC disables RB+ dual-quad mode and SX blend optimization. Two consecutive
bounded FW 5.50 runs passed the exact 18,432-pixel green SRC1 oracle with clean
exit. The Eden compatibility profile now has five feature gaps.

## Milestone 6: storage-image writes without format (2026-07-28)

`shaderStorageImageWriteWithoutFormat` is advertised and accepted through
legacy and Features2 paths. Linear RGBA8 storage images now flow through
descriptor updates and gfx1013 compute/graphics resource tables. Normal and
ASAN/UBSAN host suites pass 36/36, the Prospero target links with
`-lunwind -lc++abi -lc++ -lm`, and two bounded FW 5.50 runs verified all 4,096
checkerboard pixels with clean exit. The Eden compatibility profile now has
four feature gaps. Evidence is retained in
`20260728T154623Z-storage-image-run1.log` and
`20260728T155150Z-storage-image-run1.log`. The final gate also covers the
shared runner's duplicate Shell kill-request regression.

## Milestone 1: host ICD lifecycle

Implemented:

- All 165 Vulkan 1.0/1.1 core entrypoints are exported and present in the ICD
  proc tables. Promoted KHR aliases are available for the advertised instance
  extensions.
- Loader magic and dispatchable-object initialization for instances, physical
  devices, devices, queues, and command buffers.
- Instance/device/queue lifecycle, device groups, Vulkan 1.1 input/output
  `pNext` chains, external-capability rejection, and protected/sparse rejection.
- OpenAGC-backed GPU-visible memory, buffers and images, plus host-side views,
  synchronization, command pools, command buffers, queries, descriptors,
  render-pass/framebuffer objects, and valid pipeline-cache serialization.
- Thread-safe allocation-count enforcement, custom-allocation failure cleanup,
  and concurrent lifecycle coverage.
- Loader and Validation Layer tests. The VVL callback treats both warnings and
  errors as test failures.
- The ICD links OpenAGC transitively and consumes its versioned gfx1013
  capability snapshot instead of duplicating memory, format, sample-count,
  image-dimension, Wave32, MRT, and compute-limit definitions.

Capability evidence:

- OpenAGC's FW 5.50 qualification records establish the exposed R8/RG8/RGBA8,
  BGRA8, RGB10A2, R11G11B10, R/RG/RGBA 16/32-bit float, sRGB, D16, D32, S8,
  D16+S8, and D32+S8 format families.
- OpenAGC bounds image dimensions at 16,384 and color targets at eight.
- OpenAGC's verified PS5 memory profiles use a 4 GB write-back/onion range with
  4 KB alignment and a 12 GB write-combined/garlic range with 2 MB alignment.
- Remaining shader-stage limits use conservative gfx10/RADV bounds and do not
  enable corresponding optional Vulkan features before shader qualification.
  `tessellationShader` is the first shader-stage core feature enabled after its
  patch-output path passed the repeated FW 5.50 gate.

## Milestone 2: runtime shader pipelines

Implemented:

- Runtime VS/PS/CS/GS/tessellation compilation through the reusable
  `libopenagc_psbc` C API.
- Pipeline-context translation for vertex input, descriptor-set layouts,
  push-constant ranges, fragment specialization constants, entry points, and
  render-pass color attachment counts.
- Correct fused-stage selection for VS+GS, VS+TCS, TES NGG, and TES+GS, with
  independent Vulkan specialization maps for the primary and pre-stage SPIR-V.
- Owned AGC records and compiler metadata live with the Vulkan pipeline and are
  released deterministically by `vkDestroyPipeline`.
- A host end-to-end test compiles graphics and compute pipelines alongside the
  loader/Validation Layer test suite. The compiler's direct tests also cover
  concurrent API use.
- Host and Prospero compiler archives are selectable with
  `OPENAGC_PSBC_LIBRARY`; the installed SDK packages the selected archive and
  header transitively.

Deliberately unavailable before later milestones:

- Sparse and protected resources, external handles, YCbCr conversion, timeline
  semaphores, and descriptor indexing. Basic six-view multiview is available;
  multiview geometry and tessellation remain deliberately unadvertised.

## Milestone 3: OpenAGC command emission (qualified)

Implemented and host-verified:

- Pipeline creation relocates compiler offset-based records through OpenAGC,
  uploads executable code to 256-byte-aligned storage, and fuses NGG
  front/back records with `sceAgcFuseShaderHalves_0200`.
- Command buffers own resettable 64 KiB OpenAGC DCBs. Compute bind/dispatch
  emits gfx1013 compute state and `DISPATCH_DIRECT` with the compiler's resolved
  post-specialization local size.
- Descriptor pools own deterministic GPU-visible table storage. Standard
  storage/uniform-buffer descriptor writes and copies remain logical Vulkan
  state until dispatch, where the ICD encodes gfx1013 buffer descriptors,
  flushes the table, and asks OpenAGC to patch compiler descriptor-set
  placeholders immediately before `DISPATCH_DIRECT`. Missing sets and
  unsupported user-SGPR kinds fail command-buffer finalization.
- No-input VS/PS triangle bind/draw emits the fused Wave32 NGG state,
  interpolant state, base-vertex/start-instance user SGPRs, and
  `DRAW_INDEX_AUTO` through `agcGfx1013DrawBaselineIndexAuto`.
- Fused VS+GS pipelines now use the same typed OpenAGC primitive-shader path.
  Draw recording accepts geometry compiler metadata for vertex tables,
  base-vertex/start-instance values, and the unused zero-sized push-constant
  pointer without claiming support for nonzero push constants. The command
  regression performs a successful indexed geometry draw and verifies its PM4.
  A standalone mapped-readback ELF shrinks the triangle in the geometry stage,
  giving it a distinct coverage oracle. Two independent FW 5.500.008 runs
  produced exactly 4608 green pixels
  (`20260728T012020Z-geometry-run1.log` and
  `20260728T012034Z-geometry-run1.log`). The standalone geometry workload is
  hardware-qualified. The core feature is now exposed by both feature-query
  forms and accepted through legacy and Features2 device creation. Lifecycle
  tests cover both success paths; the standalone sample queries and requests
  geometry normally.
  Both host configurations pass all seven tests, and the Prospero ELF links
  with `-lunwind -lc++abi -lc++ -lm`; candidate SHA-256 is
  `386aae854e1aaf504a750aa29904c491e35220d52c718c3bcf048f54de6803a4`.
  Two independent bounded FW 5.500.008 runs produced exactly 4608 green pixels
  each (`20260728T051424Z-geometry-run1.log` and
  `20260728T051510Z-geometry-run1.log`). Both returned normally, bounded
  post-run websrv checks confirmed the console remained responsive, and
  neither was retried. The public feature-request path is hardware-qualified.
- Tessellation pipelines require standard `PATCH_LIST` input and compile the
  fused LS+HS stage in Wave32 alongside TES+NGG and PS. The device lazily owns
  one 256-byte-aligned factor ring, offchip ring, and descriptor table, builds
  and flushes them through OpenAGC, and registers the factor ring through the
  FW driver before recording. Draws patch ring/layout/continuation addresses,
  bind per-stage resource tables and user SGPRs, restore typed depth/stencil
  state, and emit OpenAGC's tessellation `DRAW_INDEX_AUTO`. The first FW 5.50
  attempt returned safely with a black target and exposed a missing DCB ring
  programming call. Recording now emits OpenAGC's typed tessellation-ring
  setup before the draw, and the host regression requires all four ring UC
  registers plus `VGT_TF_PARAM`. A second black result left offchip
  patch-output reads as the only material difference from OpenAGC's passing
  shader path. The basic hardware gate now follows OpenAGC's qualified
  constant-position dataflow, while patch-output reads remain explicitly
  unqualified. The revised basic workload passed twice on FW 5.500.008 with
  exactly 7200 green pixels (`20260728T013019Z-tessellation-run1.log` and
  `20260728T013031Z-tessellation-run1.log`) and is hardware-qualified at that
  scope. At that checkpoint `tessellationShader` remained false pending the
  patch-output work and repeated qualification described below.
  The patch-output-read candidate no longer uses the fixture-specific
  `TCS_OFFCHIP_LAYOUT` constant. openagc-psbc reports the linked LS/HS
  output counts, patch/control-point counts, primitive mode, and tess-factor
  reads for each compiled pipeline; the ICD validates those values and asks
  OpenAGC to build separate TCS and TES layout words. Host command-recording
  tests verify that both compiler-derived values reach PM4. Its first bounded
  FW 5.500.008 run returned
  cleanly without a hang or kernel panic but produced a zeroed target
  (`20260728T015236Z-tessellation-run1.log`); no automatic retry was attempted.
  openagc-psbc API v5 now accepts a non-executable adjacent interface module,
  and the ICD supplies TES to TCS compilation and TCS to TES/TES+GS
  compilation. This links both separately emitted programs to the same
  offchip location remap. All seven host tests and the Prospero cross-build
  pass. Its first bounded FW 5.500.008 run returned safely with a zeroed target
  (`20260728T020700Z-tessellation-run1.log`), and the loader remained
  responsive afterward. Interface linking alone therefore did not fix patch
  reads; no automatic retry was attempted and this candidate remains
  unqualified.
  Local comparison with Mesa then identified that RADV patches the hull LDS
  allocation at draw time, while the ICD had bound the compiler record with a
  zero `SPI_SHADER_PGM_RSRC2_HS.LDS_SIZE`. openagc-psbc API v6 now reports the
  required byte count and the ICD passes it to OpenAGC's typed tessellation
  binder. Host command tests require a nonzero encoded allocation. Its first
  bounded FW 5.500.008 run returned safely, left etaHEN websrv responsive, and
  produced a zeroed target (`20260728T023553Z-tessellation-run1.log`). No retry
  was attempted. Hull LDS allocation alone therefore did not fix the restored
  VS-to-TCS-to-TES dataflow, and this candidate remains unqualified.
  Mesa comparison after that run found that gfx10.3 rounds HS LDS allocation
  to 1024 bytes before encoding the result in 512-byte register units. The
  earlier OpenAGC patch rounded directly to 512 bytes and could therefore emit
  an illegal odd field value. OpenAGC now performs the two-step rounding, and
  the Vulkan command regression requires a nonzero even encoding. This
  corrected candidate returned safely from one bounded FW 5.500.008 run and
  left etaHEN websrv responsive, but its target was still zeroed
  (`20260728T024632Z-tessellation-run1.log`). No retry was attempted. Correct
  LDS rounding is retained, but it is not the remaining VS-to-TCS dataflow fix.
  Generated-code inspection then showed that the separately compiled HS back
  retained RADV's monolithic same-invocation temporary-VGPR path and compiled
  unavailable VS values as zero. openagc-psbc now disables that path in both
  halves, requires all TCS per-vertex inputs to lower to LDS, and rejects a
  surviving input intrinsic. The resulting HS ACO reads LDS and stores those
  values offchip, while TES ACO performs the expected coherent ring loads.
  Nevertheless, its one bounded FW 5.500.008 run again returned safely with a
  zeroed target (`20260728T030535Z-tessellation-run1.log`); websrv remained
  responsive and no retry was attempted. The remaining fault is downstream of
  the corrected HS input path.
  The next candidate uses an ordinary TCS storage buffer as a deterministic
  data-flow probe: three independent invocation markers prove HS execution,
  while three copied positions prove hardware LS-to-HS LDS reads. The mapped
  oracle is checked only after the bounded submission fence and cache
  invalidation. Its first bounded FW 5.500.008 launch froze graphics and the
  Shell UI recovered. ps5debug-NG then reported PID 129 faulting an SQC-data
  read at unmapped VA `0x0000000200000000`, with the wave marked
  `XNACK_ERROR MEMVIOL`; no application process remained to kill. The retained
  runner log is `20260728T031733Z-tessellation-run1.log`, and no retry was
  attempted. Generated ACO uses `s14` as RADV's indirect descriptor-set-table
  pointer for the separately compiled HS. openagc-psbc API v7 now exports that
  SGPR kind, and the ICD builds a GPU-visible low-address table of bound set
  pointers and programs the reported register before the draw. The host
  command regression requires a nonzero table pointer and set-1 entry, verifies
  the exact compiler-selected PM4 register/value pair, and rejects pointers
  outside the gfx1013 `0x2_xxxxxxxx` aperture. Both seven-test host
  configurations pass and the
  Prospero ELF links with `-lunwind -lc++abi -lc++ -lm`; corrected ELF SHA-256
  is `9be96734ae5b1643d9b1f408101cc345bb0bb7291491ed8fbdc989ab974285cc`.
  Its first bounded FW 5.500.008 run passed all deterministic oracles: hull
  markers `0x48530000` through `0x48530002`, all three copied input positions,
  and exactly 7200 green pixels
  (`20260728T034030Z-tessellation-run1.log`). It returned normally, and no
  retry was attempted. A second independent fresh-console run reproduced the
  complete hull and image oracle (`20260728T034211Z-tessellation-run1.log`)
  and left the console responsive. Patch-output tessellation is now
  hardware-qualified at this scope. The ICD advertises `tessellationShader`
  through both feature-query forms and accepts it through legacy or Features2
  device creation, while rejecting every other unadvertised core feature. The
  lifecycle test covers Features2 acceptance and unsupported geometry-feature
  rejection. The standalone sample now queries and requests tessellation
  explicitly. Both seven-test host configurations and the Prospero cross-build
  pass; the new feature-requesting ELF has SHA-256
  `a1fce3414f4fadac09ff10148d76302bac504ac3befd119e059bd3330877d30d`.
  Its one bounded hardware smoke passed the complete hull probe but returned an
  all-zero image (`20260728T034904Z-tessellation-run1.log`). The process exited
  and the console remained responsive; no retry was attempted. The log matches
  the preceding pass until the final image oracle, so the remaining defect is
  nondeterministic state or ordering after TCS execution. Feature exposure is
  unqualified again pending a correction and a repeated passing gate. The next
  standard Vulkan diagnostic makes TES write marker `0x54455300` and all three
  offchip input positions into the existing storage buffer from the unique
  `gl_TessCoord.x == 1` vertex. This separates TES launch/factor-ring state,
  offchip reads, and rasterization. Both seven-test host configurations, host
  pipeline/command execution through the expected no-GPU oracle, and the
  Prospero build pass. Candidate ELF SHA-256 is
  `b3e239c757996b7b8296719d461415913e9f1475b601553f28dd5aa06ac65c6e`.
  Its one bounded FW 5.500.008 run returned normally and left the console
  responsive (`20260728T035640Z-tessellation-run1.log`). The hull probe passed
  and TES wrote marker `0x54455300`, proving that evaluation launches, but TES
  copied zero for all three offchip control-point positions and the image was
  black. The remaining fault is before rasterization, in HS offchip stores or
  the matching TES offchip address/layout ABI. No retry was attempted.
  Inspection of the gfx10.3 offchip-ring profile then found that OpenAGC had
  allocated only one 8K-dword (32 KiB) buffer and encoded buffer count zero.
  OpenAGC commit `6406c9b` now provisions four workgroups per CU across the
  four-engine, two-array, five-CU topology: 160 buffers, a 5 MiB offchip ring,
  `VGT_HS_OFFCHIP_PARAM = 159`, and Mesa's `0x1e000` factor-ring size. Its
  validation rejects storage smaller than the encoded buffering/granularity
  profile. Both Vulkan host configurations pass all seven tests and the
  Prospero build links with `-lunwind -lc++abi -lc++ -lm`; candidate ELF
  SHA-256 is
  `316ee53df2a1b29d7dcd1c5f1c4adb3cfe0d0f07bb66f2ea41cb1d88eda9e09b`.
  Hardware qualification is pending one bounded run after a fresh explicit
  console-availability signal; no feature-stability claim is made yet.
  Two independent FW 5.500.008 runs passed the complete hull, TES, and image
  oracles (`20260728T043915Z-tessellation-run1.log` and
  `20260728T044035Z-tessellation-run1.log`). Each reproduced all three hull
  markers and LDS positions, TES marker `0x54455300` and the exact three
  offchip positions, and exactly 7200 green pixels. Both processes returned
  normally, bounded websrv checks confirmed the console was responsive, and
  neither run was retried. The repeated gate now hardware-qualifies the ring
  correction and public `tessellationShader` feature path at this scope.
- `vkCmdBindVertexBuffers`, `vkCmdBindIndexBuffer`, and `vkCmdDrawIndexed` now
  retain ordinary Vulkan binding state, encode each pipeline binding into a
  per-draw GPU-visible gfx1013 vertex table, patch the compiler-selected table
  SGPR, and emit OpenAGC's hardware-qualified `DRAW_INDEX_2` path for UINT16
  and UINT32 indices. Signed vertex offsets and first-instance values use the
  compiler metadata-selected SGPRs.
- Graphics descriptor preparation now also encodes uniform and storage buffers
  with the same validated OpenAGC buffer-descriptor path used by compute. A TCS
  command-recording test binds a storage buffer at set 1 and verifies the fused
  tessellation draw can patch its compiler-selected descriptor-table SGPR.
  Separately compiled merged stages use RADV's indirect-set ABI: the ICD writes
  bound set addresses into a transient GPU-visible pointer table and programs
  its compiler-reported SGPR rather than treating it as a direct set pointer.
- Vulkan sampler objects now translate nearest/linear filtering, mip filtering,
  repeat/mirror/clamp modes, LOD state, comparison, and standard border colors
  into OpenAGC sampler descriptors. Graphics descriptor preparation supports
  combined image samplers and separate sampled-image/sampler mappings for
  linear single-mip RGBA8/BGRA8 2D images, flushes the GPU table, and patches
  the compiler-selected VS/PS descriptor-set SGPRs before drawing.
- A standalone indexed-textured sample combines a decoy vertex, direct UINT16
  indices, interleaved position/UV input, a bilinear 2x2 RGBA8 texture, and a
  deterministic coverage/opacity/color-variation readback oracle. Host builds
  intentionally see zero pixels; the Prospero ELF cross-links successfully.
- Linear image memory requirements and `vkGetImageSubresourceLayout` expose a
  256-byte gfx1013 row pitch with derived depth/array pitches. Texture upload
  tests and the sample honor the returned layout instead of assuming tightly
  packed rows. Single-level samplers select the non-mipmapped hardware mode.
- Optimal D16, D32, S8, D16+S8, and D32+S8 images use OpenAGC's typed
  `64KB_Z_X` plane layouts, 64 KiB binding alignment, and direct-memory type.
  Render passes retain an optional depth/stencil attachment, pipeline creation
  translates static compare/write/stencil state, and each draw restores the
  typed DB surface and control after shader binding. The command regression
  uses a combined D32+S8 image and verifies transitions, separate plane
  addresses, `LESS` depth writes, and front/back `REPLACE 0x5a` stencil PM4.
- A standalone combined D32+S8 sample draws overlapping near/far and independent
  far triangles, validates green/red decisions and raw clear/near/far depth
  words, and replaces stencil with `0x5a` for every depth-passing fragment. It
  cross-links with the required target runtimes and is included in the repeated
  FW 5.50 runner. Two FW runs reported exactly 22,118 stencil writes, equal to
  `green=12288 + red=9830`, with identical near/far depth decisions.
- Inline render passes and graphics pipelines accept one to eight color
  attachments with independent static blend equations and color write masks.
  Vulkan's non-dual-source factors, five core operations, separate alpha state,
  and four constants translate to OpenAGC's typed gfx1013 blend controls before
  every baseline, geometry, indirect, or tessellation draw. The command test
  requires distinct disabled/enabled MRT targets, exact control words, target
  mask `0x6f`, and exact float constants in normal and ASAN/UBSAN builds.
  A standalone bounded probe requires opaque green on the disabled target and
  half-intensity magenta (`0x7f7f007f` or `0x80800080`, the two legal 8-bit
  UNORM tie results) on the constant-factor target, then
  exits through SystemService. Both 25-test host suites and the Prospero build
  pass. Hardware diagnostics isolated full-color output to OpenAGC's former
  unconditional `CB_COLOR_INFO.BLEND_BYPASS`; OpenAGC `d2522fa` now derives
  blend clamp/bypass and rounding policy from the target number type. Both the
  internal-path and public query/request FW 5.50 gates produced 18,432 pixels
  on each target, exact target-one `0x80800080`, clean target-process exit, and
  only the established 0x4000 baseline VM warning. `independentBlend` is
  advertised and accepted through legacy and Features2 paths; dual-source
  blend remains unsupported. The public-path Prospero ELF SHA-256 is
  `e42014fcab89df6001555faecd6a2c4a0d05edb87d9d7bcd011c62ca0caa6a99`.
  The later native-runtime migration briefly regressed static blend constants:
  per-target equations survived, but Vulkan's pipeline constants were left at
  OpenAGC's zero default, so target one stayed untouched. The repaired
  translator marks constants as native command state only when an enabled
  factor consumes them and republishes either the pipeline-static or current
  Vulkan dynamic values after each native bind. A generic packet oracle now
  requires `CB_BLEND_RED=0.25`. The corrected FW 5.500.008 candidate
  `03f9d4ec0d448a0561902067ec76698f5f7940202864b07cbc24ece757e65570`
  again produced 18,432 pixels on both targets with exact target-one
  `0x80800080`, clean system exit, no residual process, and only the accepted
  `0x4000` loader VM warning.
  Begin/end transitions cover every attachment, OpenAGC binds CB0-CB7, the
  target mask enables every active slot, and fragment export context carries
  the real MRT count. The command regression verifies CB1 and dual RGBA8
  export `0x44`.
- Static and dynamic depth bias are host-complete for baseline, indexed,
  indirect, geometry, and tessellation draws. Pipeline creation accepts
  `VK_DYNAMIC_STATE_DEPTH_BIAS`, `vkCmdSetDepthBias` records command-local
  state, and OpenAGC emits exact D16/D32 format scaling plus clamp and
  front/back slope/constant registers. Exact PM4 and pipeline regressions pass
  in both 25/25 host suites. The bounded depth-bias-clamp probe uses an
  oversized constant with a 0.125 clamp and requires D32 values 0.375 and
  0.875 instead of the unbiased 0.25 and 0.75, followed by the shared
  matching-self-kill lifecycle. Both the internal-path and public query/request
  FW 5.50 runs produced exact green 12,288, red 9,830, raw D32
  54,145/12,288/9,830, and stencil 22,118 counts, clean process exit, and only
  the established 0x4000 baseline VM warning. `depthBiasClamp` is advertised
  and accepted through legacy and Features2 paths. The public-path Prospero ELF
  links `-lunwind -lc++abi -lc++ -lm` and has SHA-256
  `f2da4d9bab0030cdfe342ed8abc42e03601f5f66fcdb47d39ce761ad42702244`.
- Static depth clamp is host-complete for baseline, indexed, indirect,
  geometry, and tessellation draws. Every frame now programs gfx1013's Vulkan
  zero-to-one clip convention and matching `ZSCALE=1`, `ZOFFSET=0` viewport
  transform; `depthClampEnable` additionally disables near and far Z clipping
  through OpenAGC's exact `0x0c080000` clip-control mask.
  The established depth sample shader now uses 0.25/0.75 instead of
  -0.5/0.5, preserving its raw D32 oracle under correct Vulkan clip space.
  Exact PM4 and pipeline regressions pass in both 24/24 host suites. The
  bounded depth-clamp probe requires a negative-Z green triangle at exact D32
  zero and a normal red control at exact 0.25, followed by the shared
  matching-self-kill lifecycle. The hardware-tested Prospero ELF links
  `-lunwind -lc++abi -lc++ -lm` and has SHA-256
  `659590336c8030c7ae118210931ac8e0ee4dac3d455c888e53d22a09cd2751b9`.
  The first bounded FW 5.50 run safely rendered the expected color coverage,
  wrote the matching stencil coverage, exited through SystemService, and left
  no stale process, but exposed OpenAGC's legacy 0.5/0.5 viewport remap after
  enabling Vulkan clip control. OpenAGC `c0dd5b4` fixes that double transform;
  exact Vulkan-PS5 PM4 regression coverage locks the corrected values. The
  corrected 2026-07-28 run passed exact green/red/raw/stencil oracles, completed
  matching SystemService self-exit, left no process, and emitted only the known
  single `amount=0x4000` baseline VM warning. `depthClamp` is now advertised and
  accepted through legacy and Features2 paths. The post-promotion Prospero ELF
  SHA-256 is
  `bcbfa074bb504ceabf352e6ecbdb1f45f112dfef70faea057d41a1eb82a9c947`.
- Fill, line, and point polygon modes use OpenAGC's typed gfx1013 raster helper
  on baseline, indexed, indirect, geometry, and tessellation draws. Exact host
  regressions require `PA_SU_SC_MODE_CNTL` values `0x128` for line and `0x008`
  for point while preserving depth-bias state; the unsupported NV rectangle
  mode is rejected. The bounded FW 5.50 probe rendered 230 green wireframe
  pixels and exactly three red point pixels with empty interiors, completed
  matching SystemService self-exit, left no process, and produced only the
  known single `amount=0x4000` baseline VM warning. The initial internal-path
  ELF SHA-256 was
  `47a15536779e194f56bb20bb8a841a92fc0ebcaf05247f4c9fab95bc1ec988e1`.
  `fillModeNonSolid` is advertised and accepted through legacy and Features2
  paths. Both host suites and the Prospero build pass. The rebuilt public-path
  probe queried and
  requested the advertised feature, reproduced the exact 230/3 oracle and
  clean lifecycle, and has ELF SHA-256
  `fd2dd48dddd46cd2519bd06fb5b9dacb6bc394658a1efc9d626b2258c9cdeeb3`.
- Point-list graphics pipelines and shader-exported point sizes now use
  OpenAGC's typed gfx1013 topology and primitive-size helpers (OpenAGC
  `949ca76`). `largePoints` is advertised and accepted through legacy and
  Features2 paths; limits report `[1, 64]` with granularity `0.125`.
  Pipeline and exact PM4 regressions cover point topology, primitive type 1,
  and the complete point-size register packet. Both normal and ASAN/UBSAN
  suites pass 26/26, and the full Prospero build links
  `-lunwind -lc++abi -lc++ -lm`. The first internal gate aborted before GPU
  submission because openagc-psbc does not lower a function-local array
  `store_deref`; replacing the three-size lookup with an equivalent ternary
  made the shader compile without changing the compiler. The corrected
  internal gate and final public query/request gate each produced exact
  8px=64, 16px=256, and 32px=1024 coverage with clean self-exit. The final
  run left PID 118 absent and emitted only the known `amount=0x4000` baseline
  warning. Public-path ELF SHA-256 is
  `439a18445742d30595b1a2e850d5e5370c8e38fc8c55a9beb09b634d3fb9130f`.
- Line-list and line-strip pipelines now translate to OpenAGC primitive types
  2 and 3. Static widths and `VK_DYNAMIC_STATE_LINE_WIDTH` use OpenAGC's typed
  primitive-size packet, with command-buffer-local dynamic state reset and
  required-before-draw enforcement. Invalid static or dynamic widths outside
  `[1, 64]` are rejected; the reported granularity is `0.125`. Exact host PM4
  regressions require 8px=`0x40`, 16px=`0x80`, and 32px=`0x100` line-control
  values. Both normal and ASAN/UBSAN suites pass 27/27, and the full Prospero
  build links `-lunwind -lc++abi -lc++ -lm`. The internal and final public
  query/request FW 5.50 gates each produced exact 8px=1,024, 16px=2,048, and
  32px=4,096 pixel coverage, clean SystemService self-exit, and only the known
  `amount=0x4000` baseline warning. `wideLines` is advertised and accepted
  through legacy and Features2 paths. The public run left PID 120 absent; ELF
  SHA-256 is
  `25db7763fd45e5494067dbcf83ed16884fcfe9a6b93958b2a9d3f3e4a63fb109`.
- `VK_EXT_shader_demote_to_helper_invocation` is now enumerated and accepted;
  Features2 reports and device creation accepts
  `shaderDemoteToHelperInvocation`. The standalone fragment probe demotes all
  even-X lanes, proves exactly 32,768 framebuffer writes are suppressed, and
  uses the demoted helper value in `dFdx` for an exact 2,048-pixel green oracle
  inside one rasterized primitive. A first fullscreen diagnostic exposed 64
  derivative-control pixels on the clipped oversized-triangle seam, so the
  qualified oracle was restricted away from the implementation-dependent
  primitive-boundary helper set rather than accepting a hardware-specific
  count. Both 28/28 normal and ASAN/UBSAN suites pass; the full Prospero build
  links `-lunwind -lc++abi -lc++ -lm`. The internal and final public FW 5.50
  gates passed exact `green=2048 blue=30720 demoted=32768`, clean matching
  SystemService self-exit, exact-PID absence, and only the known single
  `amount=0x4000` baseline warning. Public log
  `20260728T122623Z-shader-demote-run1.log`; ELF SHA-256 is
  `f9980eb6bcf1dbf96bc587fae7dd2e43be84dddfa2a8b13ac6ce6ba22e4d7327`.
- Core `shaderClipDistance` is now reported through legacy and Features2
  queries and accepted at device creation. The vertex probe exports
  `gl_ClipDistance[0] = position.x`; correct clipping retains exactly 9,216
  green pixels, leaves the left sample zero, and colors the right sample
  green. A first run used an out-of-triangle right sample and correctly
  reported the expected pixel count; the corrected oracle then passed both
  the internal gate (`20260728T123525Z-shader-clip-distance-run1.log`) and the
  final public query/request gate
  (`20260728T123848Z-shader-clip-distance-run1.log`). Both 29/29 normal and
  ASAN/UBSAN suites pass, as does the complete Prospero build with
  `-lunwind -lc++abi -lc++ -lm`. The console self-exited cleanly and target
  klogs contained only the known single `amount=0x4000` baseline warning.
  Public ELF SHA-256 is
  `64a4246ff57161364aa84cacb9377fe42b0e886ffce61851ab9329c88c163a31`.
- Core `shaderCullDistance` is reported through legacy and Features2 queries
  and accepted at device creation. The vertex probe emits two disjoint,
  equal-area triangles: every left-primitive vertex receives `-1.0`, while the
  right control receives `1.0`. Correct primitive culling retains exactly
  4,608 green pixels, leaves the left and center samples zero, and colors the
  right sample green. Both 30/30 normal and ASAN/UBSAN suites pass, as does the
  complete Prospero build with `-lunwind -lc++abi -lc++ -lm`. The internal
  gate (`20260728T124816Z-shader-cull-distance-run1.log`) and final public
  query/request gate (`20260728T124948Z-shader-cull-distance-run1.log`) passed
  the exact oracle, clean SystemService self-exit, exact-PID absence, and only
  the known single `amount=0x4000` baseline warning. Public ELF SHA-256 is
  `82ffa08623bf7632635c0009f51b439f4ae861d699c5b052cebc9bf1343dcabf`.
- Core `shaderImageGatherExtended` is reported through legacy and Features2
  queries and accepted at device creation. The fragment probe uses
  `textureGatherOffsets` with four constant offsets over a nearest-filtered,
  clamp-to-edge checkerboard. Correct extended-gather lowering redirects all
  four red components to the same red texel, producing exactly 18,432 opaque
  white pixels with center `0xffffffff` and no other color. Both 31/31 normal
  and ASAN/UBSAN suites pass, as does the complete Prospero build with
  `-lunwind -lc++abi -lc++ -lm`. The first internal run
  (`20260728T125653Z-shader-image-gather-run1.log`) already produced the exact
  pixels but exposed a verifier-label placement error; the corrected internal
  gate (`20260728T125800Z-shader-image-gather-run1.log`) and final public
  query/request gate (`20260728T130045Z-shader-image-gather-run1.log`) passed
  the exact oracle, clean SystemService self-exit, exact-PID absence, and only
  the known single `amount=0x4000` baseline warning. Public ELF SHA-256 is
  `de628e54eb7929f484715b0bec441fe8501ccf3c5560c1f01f3479926a2aa679`.
- Core `fragmentStoresAndAtomics` is reported through legacy and Features2
  queries and accepted at device creation. The fragment probe atomically
  increments one SSBO counter and writes marker `0x51a7c0de` to the uniquely
  indexed SSBO word for every rasterized pixel. Both 32/32 normal and
  ASAN/UBSAN suites pass, as does the complete Prospero build with
  `-lunwind -lc++abi -lc++ -lm`. The internal gate
  (`20260728T131002Z-fragment-stores-atomics-run1.log`) and final public
  query/request gate (`20260728T131203Z-fragment-stores-atomics-run1.log`)
  each produced exactly 18,432 framebuffer pixels, atomic increments, and
  storage writes, followed by clean SystemService self-exit, exact-PID
  absence, and only the known single `amount=0x4000` baseline warning. Public
  ELF SHA-256 is
  `ecbd369db08ae5d7dd80fb66da45d282ef5134ca3d4c614940d1a86e5a2da985`.
- Static logic operations are host-complete for baseline, indexed, indirect,
  geometry, and tessellation draws. All 16 core `VkLogicOp` values translate
  to OpenAGC's exact gfx1013 ROP3 truth tables; attachment blending is disabled
  while logic state is active, and disabled logic state restores COPY. Pipeline
  rejection and exact XOR `CB_COLOR_CONTROL=0x00660010` regressions pass in both
  25/25 host suites. The bounded probe initializes mapped RGBA8 pixels to
  `0x55aa33cc`, draws green with XOR, and requires exact `0xaaaacccc` triangle
  coverage plus unchanged background through the shared matching-self-kill
  lifecycle. Both the internal-path and public query/request FW 5.50 runs
  passed with 18,432 XOR pixels, 47,104 unchanged pixels, exact center
  `0xaaaacccc`, target-process self-exit, and only the established 0x4000
  baseline VM warning. `logicOp` is advertised and accepted through legacy
  and Features2 paths. Its public-path Prospero ELF links
  `-lunwind -lc++abi -lc++ -lm` and has SHA-256
  `aee2fa93057571ee294862c822c11f1c4ca924b55938c315e51d93968cae21e1`.
- The standalone MRT sample writes green and magenta from fragment locations
  0 and 1 to two mapped linear RGBA8 attachments. It host-builds, cross-links
  with the required target runtimes, and is included in the repeated FW 5.50
  runner. Two FW `0x05500008` runs now pass identical dual-target oracles.
- Occlusion query pools now use GPU-visible OpenAGC storage with conservative
  space for 16 render backends. Command reset writes zeroes on the GPU, begin
  and end emit typed OpenAGC ZPASS snapshots, and end publishes a separate EOP
  availability label. Host retrieval supports partial, availability, and
  32/64-bit results with bounded waits. The command regression verifies the
  reset, both snapshots, and availability release. A standalone query-enabled
  triangle requires the 64-bit occlusion result to equal mapped pixel coverage;
  it host-builds and cross-links for Prospero with the required runtimes.
  Two isolated live-query runs and both final-suite runs returned exactly
  18,432 samples for 18,432 mapped pixels. Timestamp valid bits remain zero.
- `VK_EXT_host_query_reset` is advertised and accepted during device creation.
  Its feature query reports true, and `vkResetQueryPoolEXT` zeroes and flushes
  exactly the requested GPU-visible query slots. Lifecycle coverage verifies
  extension enumeration, enablement, dispatch lookup, reset, and unavailable
  result semantics.
- FW `0x05500008` produced two deterministic passes each for depth and MRT in
  retained run `20260727T231245Z`. The first query submission then hung the GPU.
  Audit identified an invalid command-reset `WRITE_DATA` configuration; it now
  uses hardware-proven destination 2 with control `0x00100100`. Staged recovery
  qualified host reset, command reset, idle begin/end, and live counting before
  the final repeated suite passed.
- The command-recording regression compiles ordinary SPIR-V compute and
  triangle shaders and verifies the real PM4 opcodes and dispatch/draw counts.
- A standalone, application-neutral compute sample now exercises the public
  Vulkan path end to end: storage-buffer allocation and descriptors, runtime
  pipeline compilation, dispatch, fence wait, mapped-memory invalidation, and
  deterministic verification of 1,024 words. It cross-links as a Prospero ELF
  against `libunwind`, `libc++abi`, `libc++`, and `libm`.
- The first dispatch in each command buffer emits OpenAGC's complete FW
  5.50-qualified gfx1013 compute-default register groups before compiled
  pipeline state. Omitting these defaults stalled the hardware stream before
  its EOP completion write.
- Render-pass and framebuffer objects now retain real attachment state. An
  inline single-color pass translates Vulkan layouts to OpenAGC transitions,
  emits `agcGfx1013BuildFramePrologue`, binds the image allocation as CB0, and
  applies fixed viewport/scissor state before `DRAW_INDEX_AUTO`. Draws outside
  an active pass and unsupported clear/dynamic/multisample/logic-op state fail
  command-buffer or pipeline creation instead of recording incomplete state.
- The standalone triangle sample uses only Vulkan 1.1 APIs and verifies a
  mapped 256x256 linear RGBA8 result after a VS/PS draw and fence wait. Both
  compute and triangle samples now cross-link as Prospero ELFs with the target
  C++ and math runtimes.

FW 5.50 compute/triangle hardware gate:

- OpenAGC Prospero initialization now prepares GPU authorization before
  `/dev/gc` access, including the FW 5.50 detached-thread ucred case. The
  compute and triangle ELFs were relinked against that implementation, closing
  the prior hidden dependency on a sample-only credential header.

- Shader executable uploads and queue submission storage now use OpenAGC
  flexible GPU-visible allocations. `vkQueueSubmit` copies each recorded DCB
  into the serialized queue mapping, appends a monotonic EOP `RELEASE_MEM`,
  submits through OpenAGC, and performs a bounded label wait before signaling
  Vulkan semaphores and fences. The host backend captures and verifies the
  submitted packet, including the appended release.
- Vulkan memory type 0 uses flexible write-back GPU memory; type 1 uses
  2 MiB-aligned write-combined direct memory. Mapping, flush, invalidate, and
  destruction delegate to OpenAGC and preserve the advertised heap semantics.
- Storage-image and texel-buffer descriptors, dynamic buffer offsets, push
  constants, depth/stencil clears, color blending, and general
  dynamic raster state are not emitted yet.
- On FW `0x05500008`, `examples/run_fw550_m3.sh` completed two consecutive
  compute runs with `PASS 1024 deterministic values` and two consecutive
  triangle runs with `PASS 18432 green pixels`. Two indexed-textured runs each
  reported `PASS 18432 pixels 64+ colors`. This additionally qualifies indexed
  vertex fetch, fragment sampled-image/sampler tables, bilinear filtering, the
  256-byte linear texture row pitch, and mapped render-target readback.
- `examples/run_fw550_m3.sh` remains the authoritative regression gate: it runs
  compute, triangle, indexed-textured, depth, MRT, and query twice through
  foreground websrv, rejects a missing PASS oracle, and retains per-run output
  without committing runtime logs. The expanded `20260728T003956Z` gate passed
  all 12 runs with exact deterministic depth, stencil, MRT, and query oracles.
- Query qualification now uses explicit `lifecycle`, `reset`, and `full` probe
  stages. The lifecycle stage emits no query PM4 and passed FW `0x05500008` on
  2026-07-28 with the qualified triangle oracle (`green=18432`). The reset-only
  stage also passed with `green=18432`, qualifying the corrected command-reset
  `WRITE_DATA` independently. An `idle` probe brackets no draw and expects an
  available zero result, isolating ZPASS/`DB_COUNT_CONTROL` before live sample
  counting. It passed with `samples=0 available=1`, qualifying the complete
  begin/end/availability sequence without rasterization. The full live-draw
  query then passed twice with `samples=18432 green=18432`; query is restored
  to the repeated Milestone 3 regression suite. Because this is an exact
  independent coverage match rather than a boolean approximation, the same
  ZPASS path now backs advertised `occlusionQueryPrecise`; the sample requests
  the feature and uses `VK_QUERY_CONTROL_PRECISE_BIT`. The staged probes remain
  for isolating lifecycle, reset, idle-ZPASS, and live-counting regressions.

## Milestone 4: VideoOut WSI (FW 5.50 hardware qualified)

Implemented:

- Standard `VK_KHR_surface`, `VK_EXT_headless_surface`, and
  `VK_KHR_swapchain` entrypoints and proc-table dispatch, including device-group
  present queries and `vkAcquireNextImage2KHR`.
- A fixed, capability-derived 1920x1080 BGRA8-sRGB FIFO surface contract with
  three Vulkan-owned linear images backed by direct write-combined memory.
- Thread-safe acquire tracking, binary semaphore/fence handoff, FIFO present
  after the queue's synchronous EOP completion, bounded VideoOut waits, and
  old-swapchain retirement/recreation. Exhausted acquire calls wait against
  their monotonic timeout, and present does not hold the swapchain lock while
  waiting for VSYNC.
- OpenAGC owns the FW 5.50 linear-registration patch, immediate byte-verified
  restoration, VideoOut registration, flip equeue, and deterministic teardown.
- Direct host WSI tests cover enumeration, `VK_INCOMPLETE`, exhaustion,
  acquire/present synchronization, device groups, and recreation. All nine ICD
  tests, both runner safety simulations, and the WSI-enabled Validation Layers
  test pass with zero messages.
  The WSI test also releases an image from a delayed presentation thread and
  proves a blocked acquire wakes with that image.
- `vulkan_ps5_swapchain_example` completes 1,800 host frames and its Prospero
  ELF links with `-lSceSystemService -lunwind -lc++abi -lc++ -lm`; candidate
  SHA-256 is
  `d94722b2c9473b8407769b9b1fe044dd5796c6d5f78bbba7ccec15cfb6975c90`.

Qualification history:

- The first candidate exited on an execute-only read while verifying
  the VideoOut patch (`20260728T053743Z-swapchain-run1.log`); the kernel log
  localized it to `libSceVideoOut.sprx+0x7e61`. OpenAGC `290213c` moves byte
  verification inside the RWX window and restores RX on every path. The second
  run (`20260728T054235Z-swapchain-run1.log`) reached 1,800 frames, but a
  teardown SIGSEGV and `0x4000` VM resource leak occurred after the premature
  PASS line. OpenAGC `18011af` now closes VideoOut before deleting the equeue
  and omits the unqualified explicit flip-event deletion. The sample now emits
  PASS only after complete Vulkan teardown. The corrected run
  (`20260728T054859Z-swapchain-run1.log`) reached both cleanup checkpoints, but
  returning from the ELF entrypoint jumped to `main+0xbb` (`RIP 0x4000bb`) and
  caused SIGSEGV plus a `0x4000` VM resource leak. The `thr_exit` candidate
  (`20260728T055807Z-swapchain-run1.log`) then completed 1,800 frames and Vulkan
  teardown with no process left behind, but hbldr's HTTP request timed out after
  60 seconds. The following libc `exit` candidate
  (`20260728T060157Z-swapchain-run1.log`) also completed 1,800 frames and Vulkan
  teardown but left PID 145/app ID `0x16` owning a black screen. A guarded
  `sceSystemServiceKillApp` recovery removed that application and restored the
  home screen, confirmed visually. The sample now resolves its app ID through
  `sceSystemServiceGetAppStatus`, requests app-level termination after Vulkan
  cleanup, and keeps the main thread alive until SystemService finishes. The
  helper is `_Noreturn`; disassembly verifies that `main` ends with its call
  followed by `ud2` and that both helper paths remain in sleep loops. A
  recovery ELF refuses to act unless exactly one other `eboot.elf` exists. The
  next bounded run (`20260728T062155Z-swapchain-run1.log`) completed 1,800
  frames and Vulkan cleanup. Klog recorded the self-requested `KillApp()`, `All
  processes exited`, and shell focus restoration; the exact PID was absent and
  websrv remained responsive. Its only failure was a `0x4000` VM resource
  warning, exactly matching OpenAGC's standalone multi-submit trailer
  allocation. OpenAGC `1c0fb8f` now carves the 64-byte trailer from unused
  `SceGnmDdid` space instead of allocating another 16 KiB VM resource. The
  follow-up (`20260728T063200Z-swapchain-run1.log`) reproduced the warning
  after another otherwise clean 1,800-frame lifecycle, falsifying the trailer
  hypothesis. OpenAGC `0c22e06` now restores the temporarily writable VideoOut
  text range to its original execute-only protection instead of read/execute,
  allowing the kernel to coalesce the mapping. The next gate
  (`20260728T063634Z-swapchain-run1.log`) reproduced the warning,
  falsifying that hypothesis too. All 27 retained OpenAGC graphics klogs carry
  the identical one-page warning. OpenAGC `4f66aa7` now explicitly unregisters
  the flip event before closing VideoOut and deleting its still-live equeue.
  The runner has no automatic retry and now
  takes a post-run PID-scoped klog snapshot, rejects fatal signals, app crashes,
  XO faults, and warnings beyond the proven single raw-ELF baseline, requires a self-requested kernel `KillApp()` followed
  by `All processes exited`, and
  requires exact-PID process absence before PASS. Post-PASS failures also
  attempt cleanup of that exact PID.
- The balanced flip-event lifecycle run
  (`20260728T064111Z-swapchain-run1.log`) again completed 1,800 frames, Vulkan
  cleanup, self-KillApp, exact-PID removal, and shell restoration without a
  crash, but retained the warning. `vulkan_ps5_system_exit_probe` now isolates
  the raw-ELF/SystemService path with no Vulkan, OpenAGC, GPU, VideoOut, equeue,
  or custom-memory work. Its bounded runner distinguishes clean teardown from
  exactly one matching `0x4000` baseline warning and rejects other warnings or
  fatal lifecycle evidence. Probe ELF SHA-256:
  `e585e74f872a4dfc7fa63910437b106843334666157672f9959c27558afe06a9`.
  Its bounded hardware run
  (`20260728T064628Z-system-exit-probe-target.klog`) produced the exact same
  warning without any Vulkan, OpenAGC, GPU, VideoOut, equeue, or custom-memory
  work, then completed self-KillApp, exact-PID removal, and the console probe.
  The warning is therefore FW 5.50/raw-ELF bookkeeping. Combined with the
  balanced 1,800-frame swapchain run, this closes the Milestone 4 hardware
  gate.

## Milestone 5: reusable installed SDK (FW 5.50 hardware qualified)

Implemented and verified:

- The independent package consumer includes only `<vulkan/vulkan.h>`, calls
  standard Vulkan 1.1 entrypoints, and completes instance, physical-device,
  queue-family, and logical-device lifecycle checks.
- `vulkan_ps5.package_relocation` installs Vulkan-Headers, OpenAGC,
  `libopenagc_psbc.a`, and `libvulkan_ps5.a` into a fresh prefix, moves the
  complete SDK, and builds the consumer from a separately copied source tree.
- The test rejects source-workspace paths in installed CMake metadata and the
  external consumer's link command. The host relocation run passes and the
  complete host suite passes 11/11.
- The Prospero relocation run passes using the relocated
  `VulkanPS5::ICD` target. Its link audit proves the three relocated archives
  plus transitive `-lkernel`, `-lSceVideoOut`, `-lunwind`, `-lc++abi`,
  `-lc++`, and `-lm`, and rejects an installed `SceAgcDriver` dependency.
  `SceSystemService` remains an explicit application lifecycle dependency
  rather than an ICD dependency.
- The normal Prospero driver/examples build remains clean after the package
  test was added. The retained installed-package ELF has SHA-256
  `1fd79429140e26884cc492761d8551cf7a2b8769c39066463d9f05f6c9fc5547`.
- `run_fw550_package_consumer.sh` reuses the proven bounded raw-ELF lifecycle
  gate with a package-specific ELF and PASS oracle. Its simulation verifies
  exact-PID handling and acceptance of only the proven single `0x4000`
  baseline warning.
- The current consumer creates two devices/queues concurrently, exercises a
  timeline semaphore, destroys one, and performs a survivor memory
  allocation/free. Its exact FW 5.500.008 candidate passed once without retry.
  PID 154 printed
  `package-consumer: PASS result=0`, was absent in the exact-PID ps5debug-NG
  check, and left no warning beyond the proven raw-ELF `amount:0x4000`
  baseline. Evidence: `20260731T065637Z-package-consumer.log` and
  `20260731T065637Z-package-consumer-target.klog`.

This closes Milestone 5 at the reusable static SDK/package scope.

## Milestone 6: Eden compatibility profile (ICD profile complete)

The final `multiViewport` closure brings the automated live report to
`extensions=0 features=0 limits=0 queues=0 total=0`. Eden's separate Prospero
surface/build/static-entrypoint integration remains outside this ICD profile.

`variablePointers` and `variablePointersStorageBuffer` are public through the
standalone and Vulkan 1.1 Features2 paths, and both request forms are accepted
at device creation. The bounded 64-invocation SPIR-V probe exercises both
branches of selected StorageBuffer and Workgroup pointers with an exact
1,024-dword oracle. The crash investigation isolated a cross-layer wave-mode
mismatch: openagc-psbc emitted Wave32 compute code, while Vulkan left the
gfx1013 dispatch initiator at its zero Wave64 default. Vulkan now sets
OpenAGC's named Wave32 modifier; no shader-side barrier or application
workaround is required. Both 34/34 normal and ASAN/UBSAN suites, the release
Prospero build, and repeated public FW 5.50 gates pass. Evidence is retained
in `20260728T144957Z-variable-pointers-run1.log` and
`20260728T145026Z-variable-pointers-run1.log`. The live Eden profile is now
six feature gaps, and the public ELF SHA-256 is
`d6d0669f82d2fcd7bac06099eaee6aa9511c8620744548d0a952001779d2702f`.

Initial audit at `../eden-ps5` revision `39763e7321`:

- Eden requires Vulkan 1.1, four device extensions, 29 feature bits, four
  explicit minimum limits, a graphics/present queue, and swapchain support.
- Vulkan-PS5 passes Vulkan 1.1, all four limits, the universal queue shape,
  `VK_KHR_swapchain`, `geometryShader`, `tessellationShader`, and
  `hostQueryReset`.
- The hard startup baseline is four missing extensions and 26 missing feature
  bits. `vulkan_ps5_eden_profile_test` reports every name and a stable category
  summary; its normal reporting mode is registered with CTest and `--strict`
  remains nonzero until the profile is actually complete.
- `VK_KHR_driver_properties` now identifies the experimental gfx1013/ACO path
  with a deliberately non-conformant `0.0.0.0` version.
  `VK_KHR_shader_float_controls` exposes a complete, conservative property
  record with no unqualified execution-mode capabilities. Enumeration,
  Properties2 chaining, and device enablement are lifecycle-tested. The
  precise-occlusion contract now exposes the same exact-count ZPASS path that
  repeatedly matched the independent FW 5.50 mapped-pixel oracle at
  `samples=18432 green=18432`. Legacy/Features2 reporting, device enablement,
  and precise command recording are host-tested, and the standard query sample
  requests the feature and flag normally. The 2026-08-01 current-commit replay
  exposed a teardown-order regression in the reset, idle, and full variants:
  their command buffers still retained the native query buffer when the query
  pool was destroyed. Recycling the command pool first fixes the ownership
  order. All four query variants then passed their rendering/result oracles and
  destroyed the device cleanly with hashes `21321555` (lifecycle), `e843380e`
  (reset), `0c053a70` (idle), and `49f1df4a` (full; SHA-256 prefixes). The
  guarded runner now rejects any OpenAGC lifecycle failure even when the
  application prints its rendering PASS line. Evidence is in
  `20260801T131240Z-query-lifecycle.log`,
  `20260801T131251Z-query-reset.log`, `20260801T131302Z-query-idle.log`, and
  `20260801T131313Z-query-full.log`. The live profile is now two
  extension plus 25 feature gaps, total 27.
- The compatibility audit is refreshed against upstream Eden revision
  `612409c7ba`. VMA passes, and the format subset includes all 14 BC1-BC7
  variants with multi-mip cube/cube-array and nonzero mip-view coverage. D24,
  ASTC, and ETC remain fail-closed or use Eden's recorded transcode paths; the
  direct uncompressed image inventory is complete. Eden's PS5 branch now has
  the surface/static-entrypoint bridge and bounded FW 5.50 bootstrap evidence.
  The hard startup profile is now zero-gap after fixing and restoring
  `geometryShader`. Current
  generic tests pass 61/61 and the static/shared Prospero build is clean.
- The application-neutral color-clear command is complete for every advertised
  uncompressed color format. `vkCmdClearColorImage` packs arbitrary 1/2/4/8/16
  byte UNORM, SRGB, half, float, RGB10A2, and R11G11B10 values over exact
  mip/layer intervals through a reproducible internal compute pipeline and only
  public OpenAGC pipeline/descriptor/push/dispatch APIs. OpenAGC commit
  `1288666` snapshots rebound descriptor/push/vertex tables, while `b057690`
  keeps reflected inline-push updates allocation-free. Regular array layers
  batch into one two-dimensional dispatch per mip. Normal and ASAN/UBSAN suites pass
  41/41, including VVL RGBA16F coverage, allocation-failure cleanup, and
  fail-closed compressed/depth cases; Prospero static/shared builds pass.
  Hardware pixel execution for this exact slice remains pending.
- `vkCmdClearDepthStencilImage` is hardware-qualified on FW 5.500.008 for the
  advertised D16, D32,
  S8, D16+S8, and D32+S8 single-sample formats. It validates Vulkan layout,
  usage, aspects, normalized depth, and complete selected ranges before
  recording; independently queried depth/stencil planes are filled through the
  same public-OpenAGC meta compute pipeline, with regular array layers batched
  per plane and mip. A 70-layer D32+S8 gate records exactly two dispatches for
  one selected mip. Clean 56/56 normal and ASAN/UBSAN suites pass, and the
  Prospero static/shared libraries build clean. A pinned ELF passed twice on
  FW 5.500.008 with exact readback across all five formats (`depth=256
  stencil=192`), bounded synchronization, teardown, and immediate relaunch.
  D24 and multisampled clears remain fail-closed; partial attachment clears and
  the identical FW 11.60 replay remain pending.
- Depth-only graphics pipelines are now accepted with zero dynamic-rendering
  color formats and a zero-attachment color-blend state. The D32 host gate
  compiles a fragment shader that exports only depth and proves creation of a
  native OpenAGC graphics pipeline. This is the application-neutral prerequisite
  for depth/stencil attachment-clear draws and ordinary depth-only passes.
- `analysis/eden-compatibility.md` records the evidence, prevents Eden's
  continue-after-unsuitable behavior from being mistaken for support, and
  defines the application-neutral implementation order.
- A deterministic `vulkan_ps5_mirror_clamp_probe` and one-shot FW 5.50 runner
  qualify `VK_KHR_sampler_mirror_clamp_to_edge`. The probe uses an
  out-of-range texture coordinate whose expected gray readback distinguishes
  gfx1013 mirror-once from edge clamp. The runner requires an exact PASS
  oracle, rejects PID-scoped crashes/XO violations and unexpected warnings,
  proves exact-PID process absence, and rechecks websrv without retrying.
  Runner safety coverage, both 25-test host suites, and the Prospero build
  pass. Both the internal-path and extension-enabled FW 5.50 runs produced
  18,432 gray pixels with exact center `0xff808080`, matching SystemService
  self-exit, no stale process, and clean target-only klog. The extension is
  enumerated and accepted at device creation. The public-path ELF SHA-256 is
  `6b591dfe79c69f32cc9efdb641ab686183b0c7c0e032df7f3892f6e3357ce78f`.
- openagc-psbc API v8 and Vulkan pipeline creation now carry instance-rate
  vertex attributes and nonzero per-binding divisors through RADV's gfx1013
  vertex-input lowering. Unlisted instance bindings correctly default to
  divisor one; duplicate, missing, vertex-rate, and zero-divisor entries are
  rejected rather than silently miscompiled. The compiler regression verifies
  the start-instance SGPR, and the Vulkan pipeline regression compiles divisor
  two and rejects zero. Legacy EXT and promoted property queries report
  `UINT32_MAX` for the nonzero compiler range; legacy EXT and promoted Features2
  queries expose instance-rate divisor support while zero divisor and
  nonzero-first-instance support remain false. Device creation accepts the
  proven feature and rejects zero divisor. Both 25-test normal and ASAN/UBSAN
  suites, openagc-psbc host tests, and both Prospero builds pass.
  `VK_EXT_vertex_attribute_divisor` is enumerated and accepted.
- The bounded vertex-divisor hardware gate draws four
  overlapping instances from red/white/green/blue instance data: divisor two
  must leave an exact-white single-color triangle, while divisor one leaves
  blue and vertex-rate handling produces multiple colors. The runner permits
  one launch, scopes fatal klog checks and cleanup to the exact PID, proves
  post-return absence and websrv availability, and accepts only the established
  single raw-ELF `0x4000` warning. Both the internal-path and public
  extension/feature-enabled FW 5.50 runs produced 18,432 exact-white pixels
  with center `0xffffffff`, matching self-exit, no stale process, and clean
  target-only klog. The public-path Prospero ELF SHA-256 is
  `5647b97d9ad8944028c4e242c49503a36f307ecc9ff603d765aec2f56b0c1503`.
- Eden's actual VMA allocation model is now covered by a configurable,
  test-only consumer. It supplies only dynamic instance/device proc lookup,
  enables external synchronization, selects Eden's preferred large-block size,
  and exercises mapped upload, download, and stream buffers; device-local
  buffers and images; within-budget allocations; shared block suballocation;
  and the manual `vmaAllocateMemoryForBuffer`/`vmaBindBufferMemory2` and raw
  requirements paths. VMA 3.3 keeps Eden's exact AUTO manual usage; the newer
  available VMA 3.4 header uses its required explicit equivalent. Direct ICD
  and loader/VVL variants pass with deterministic zero-allocation teardown.
  The complete host suite is now 17/17. The Prospero VMA candidate builds as a
  PIE with the exported `-lunwind -lc++abi -lc++ -lm` dependencies and SHA-256
  `8c7d669a9bef3acfcb1054eeb0327e4d51d645338374fa1202a9e640dd5f2871`.
  Its bounded one-shot runner has clean/crash safety coverage, exact-PID fatal
  checks and cleanup, and post-run websrv validation. The first FW 5.50 run
  printed the complete PASS oracle before normal C++ return/exit dispatch
  faulted at `RIP 0x4000bb` (PID 154). The probe now flushes PASS and requests
  application-level SystemService termination, matching the already qualified
  raw-ELF lifecycle without running that broken destructor path. The next
  single bounded run passed every allocation oracle, removed exact PID 155,
  left websrv responsive, and emitted only the established raw-ELF `0x4000`
  warning. Evidence is retained in
  `20260728T075658Z-eden-vma-run1-target.klog` and
  `20260728T075920Z-eden-vma-run1.log`; VMA runtime allocation patterns are
  hardware-qualified at this scope.
- The sampler-anisotropy path validates the Vulkan 1x-16x range,
  switches point/linear requests to gfx1013 anisotropic filter modes, and
  encodes the quantized maximum with OpenAGC's typed descriptor helper. Direct
  host tests cover valid 8x creation and invalid low/high ratios. A standalone
  probe now draws identical high-frequency stripe footprints with bilinear and
  16x-anisotropic samplers into separate target halves. Its oracle checks equal
  coverage and neutral means, proves that the bilinear control aliases, and
  requires the anisotropic mean absolute deviation to be at least 25% lower.
  `samplerAnisotropy` is now `VK_TRUE` through legacy and Features2 queries and
  is accepted through both device-creation paths. Both full 25-test host suites
  pass. The internal and public FW 5.50 gates each reported
  `linear=9830/127/63 aniso=9830/127/5`, removed exact PIDs 104 and 105, and
  left clean PID-scoped klogs. Evidence is retained in
  `20260728T111713Z-sampler-anisotropy-run1.log` and
  `20260728T112026Z-sampler-anisotropy-run1.log`. The public Prospero ELF links
  `-lunwind -lc++abi -lc++ -lm` and has SHA-256
  `28319bef31f227ea45b9aacc35138e10b9cb136b88dbba350bc2a562a16c49b9`.
- `vkCmdDrawIndirect` and `vkCmdDrawIndexedIndirect` now use OpenAGC's typed
  gfx1013 indirect path. Common draw preparation binds shaders, frame/depth
  state, descriptors, and vertex tables while openagc-psbc API v9 supplies the
  compiler-selected base-vertex, start-instance, and DrawIndex registers.
  Buffer usage, alignment, stride, full command range, and index bindings are
  validated; zero draw count remains a legal no-op. DrawIndex-using multi draws
  expand into single packets with explicit values 0..N-1. This avoids the
  speculative 10-dword gfx10+ packet that caused PID 156 to receive a fatal GPU
  signal and triggered a GPU reset during the bounded 2026-07-28 run. OpenAGC
  restored the earlier FW 5.50-qualified 7-dword PS5 packet for shaders that do
  not consume DrawIndex.
  A deterministic hardware gate now packs two commands into one indirect
  buffer. Both use `firstVertex = 1` to skip a decoy; nonzero first instances
  1 and 2 plus DrawIndex values 0 and 1 must produce an all-green image split
  equally across the two target halves. Diagnostic PIDs 106 through 109 exited
  without a fatal signal or GPU reset and isolated the failure to the combined
  BaseInstance/DrawID metadata path. Mesa RADV uses BaseVertex, DrawID, then
  BaseInstance for gfx10 vertex user SGPRs, while openagc-psbc had exported the
  last two in reverse order. Compiler commit `d209d94` fixes and regression-tests
  that metadata order.
  The internal PID 110 and public query/request PID 111 gates both reported
  `green=11472 left=5736 right=5736 firstVertex=1 firstInstance=1,2 drawID=0,1
  draws=2`, completed matching SystemService exit, and left no PID-scoped fatal
  signal or GPU reset. Evidence is retained in
  `20260728T113157Z-indirect-draw-run1.log` and
  `20260728T113410Z-indirect-draw-run1.log`. The bounded runner has clean/crash
  exact-PID safety coverage, both 25/25 host suites pass, and the public
  Prospero ELF links `-lunwind -lc++abi -lc++ -lm` with SHA-256
  `d3d80356967a93a9a8cfa0000eaa09089ddd39872f897e73952fe788ab48aa29`.
  `multiDrawIndirect`, `drawIndirectFirstInstance`, and
  `shaderDrawParameters` are advertised and accepted through their standard
  core, Features2, Vulkan 1.1, and standalone feature paths.

The core `vertexPipelineStoresAndAtomics` contract is implemented and
hardware-qualified. Legacy and Features2 queries report it, and device
creation accepts it through both paths. The combined VS/TCS/TES/GS probe
performs an atomic exchange and direct SSBO store in every applicable stage,
then requires eight exact markers and exactly 7,200 green pixels. The fused
TES-to-GS recorder now preserves both its vertex resource table and descriptor
tables instead of rejecting or overwriting the former. Both 33/33 host suites,
runner safety coverage, and the complete Prospero build pass. Repeated public
`20260728T133417Z` and `20260728T133515Z` FW 5.50 gates passed the exact
oracle with matching SystemService exit, no stale process, and only the known
single `amount=0x4000` baseline warning. The live Eden profile is now eight
feature gaps. The public Prospero ELF SHA-256 is
`e79e33fe4bc5c8f780e1801456e3ea9bae4a1034148d873b039563ac11dd171a`.
