# Vulkan-PS5 Native Migration Audit — 2026-07-31

Historical and current audit of Vulkan-PS5's direct low-level OpenAGC calls.
The mechanically checked inventory reached **exactly zero** on 2026-07-31
while preserving one ordered native Vulkan queue stream.

## Current execution revision

The active goal order is now:

1. **WSI → `AgcPresentChain`** (complete, FW 5.50-qualified): the three raw
   `agcVideoOut*` symbols are gone; the later shader slice reduces the checked
   inventory to 26.
2. **Transfer-image region/buffer forms** (complete host slice): OpenAGC API
   41 owns color/BC regions and buffer↔image copies; every remaining
   clear/blit/resolve form fails closed.
3. **Shader ownership** (complete): code allocation, relocation, flush, and
   fused shader halves are fully behind `AgcShader`; direct draw/dispatch
   recording is native-only.
4. **Descriptor/tessellation ownership and legacy submission deletion**
   (complete, host-qualified): the duplicate encoder, raw auxiliary
   allocations, fence labels, and submit path are deleted; the audit is zero.

OpenAGC API 39 and Vulkan-PS5 completed `agcCmdUpdateBuffer` and
`agcCmdFillBuffer` after the initial audit. Those two commands are no longer
silent stubs. OpenAGC API 40 then admitted the existing BGRA8-SRGB scanout
format through the native present-chain contract. Vulkan WSI now creates
scanout-qualified dedicated `AgcImage` allocations, initializes them to
`VideoOutScanout` on the native graphics queue, retains the completed submit
fence as the presentation dependency, and uses condition-variable acquire
wakeup instead of polling. The authoritative audit is now 34.

OpenAGC API 41 then added versioned `AgcImageCopyRegion` and
`AgcBufferImageCopyRegion` records plus typed image-region and buffer/image
commands. Vulkan maps partial mip/layer copies and explicit buffer strides to
those commands on the ordered native graphics stream. The five unsupported
clear/blit/resolve entry points now latch `VK_ERROR_FEATURE_NOT_PRESENT`; none
can return a successful empty recording. This correctness slice intentionally
does not change the 34-symbol low-level inventory.

## Current checkpoint: native runtime boundary complete (26 → 0)

Vulkan-PS5 now contains no direct `sceAgc*`, `sce_agc_*`, `agcCb*`,
`agcGfx1013*`, `agcGpuMemory*`, or `agcVideoOut*` call. Descriptor writes and
vertex/index bindings use typed command APIs. OpenAGC owns sampler border
tables, shader records, tessellation offchip/factor rings, image layouts,
command storage, queue submission, finite fence waits, and present chains.

The legacy `SceAgcCb` storage, raw descriptor/vertex tables, flexible-memory
allocations, EOP labels, DCB submission fallback, tessellation setup, render-
pass PM4 prologue, and duplicate resource transitions were removed. Vulkan
image creation now obtains layouts only through `agcGetImageLayout` and
`agcGetImageSubresourceLayout`. Unsupported command mixtures fail recording
closed; queue submission never falls back to a second encoder.

`analysis/native_runtime_calls.tsv` intentionally contains only its schema
header. `tests/native_migration_audit.py` accepts that zero-row inventory and
continues to fail if a direct low-level symbol reappears. Normal and sanitizer
host builds pass all 46 CTest gates, and the Prospero target set builds cleanly.
The focused FW 5.500.008 custom-border gate then passed `covered=18432
blue=18432 swizzle=BR`, self-exited, and left only the established `amount=0x4000`
warning; see `examples/qualification-logs/20260731T155934Z-custom-border-color-run1.log`.
That smoke test does not replace the remaining full FW 5.50 candidate sequence.

## Historical checkpoint: shader execution became native-only (34 → 26)

The ICD no longer owns shader-code allocations, record relocation, cache flush,
or front/back fusion. `agcCreateShader` copies and relocates complete PSBC
records and owns their lifetime. `vkCmdDispatch`, direct/indexed draws, and the
exercised geometry/tessellation forms now record only through typed native
OpenAGC commands; missing typed state fails command recording closed.

Eight audited symbols were removed: the compute default/dispatch pair, four
legacy direct/tessellation draw helpers, the border-table draw helper, and the
compatibility shader-fusion export. Pipeline-switch coverage also exposed and
fixed stale native descriptor/vertex caches: a changed pipeline or descriptor
set now rebinds rather than returning an incomplete stream. Command tests use
native draw/dispatch counts as their active oracle.

At this historical checkpoint the mechanically enforced TSV contained 26
symbols. The later native-boundary slice removed all of them.

FW 5.500.008 exposed and closed the border-table runtime gap behind this
reduction. The first native-only custom-border submission failed before its
fence; API 42 moved the table allocation, 128-bit entry upload/flush, and base
register programming into OpenAGC. Candidate
`53b5f333d704220c91d291d2254534676a7bf6888e0112a30e047109d3d8e025`
then passed all 18,432 swizzled-blue samples with clean process teardown.

## 1. Historical pre-shader inventory: how the 34 calls clustered

The mechanically-checked gate is `tests/native_migration_audit.py`, which scans
`src/*.c` for `sceAgc*|sce_agc_*|agcCb*|agcGfx1013*|agcGpuMemory*|agcVideoOut*`
and requires each symbol to be owned by `analysis/native_runtime_calls.tsv`.
The inventory is **per-symbol, not per-call-site** — a symbol only leaves the 34
when *every* call site is migrated. That detail drives the sequencing below.

Mapping each of the 34 symbols to its real call sites and what unblocks full
removal:

| Cluster | Symbols (count) | Call sites | Removed when |
|---|---|---|---|
| **Legacy DCB submit** | `agcCbInit`, `agcCbAllocDwords`, `agcGfx1013SignalEopFence`, `sceAgcDriverSubmitDcb`, `agcGpuMemoryWait32` (5) | `src/vulkan_ps5.c:231,232,243,257,264` (`vk_ps5_queue_submit_dcb`) | the duplicate encoder is deleted (every command buffer is `native_stream_complete`) |
| **Legacy command-buffer cursor** | `agcCbReset`, `agcCbUsedDwords` (2) | `vulkan_ps5_core.c:1948,2137,2232,630`; `vulkan_ps5.c:247` | legacy `command->dcb` storage is deleted |
| **Duplicate graphics encoder** | `agcGfx1013BuildFramePrologue`, `agcGfx1013InitColorTarget`, `agcGfx1013TransitionResource`, `agcGfx1013SetColorBlendState`, `agcGfx1013SetPrimitiveSizeState`, `agcGfx1013ApplyPolygonMode`, `agcGfx1013SetDepthBiasState`, `agcGfx1013DrawBaselineIndexAuto`, `agcGfx1013DrawBaselineIndexed` (9) | `vulkan_ps5_core.c:8094,8055,8070/8158/8263/8309,6866,6887,6894,6937,7583,7580` | every graphics command records only on the native stream |
| **Duplicate tessellation encoder** | `agcGfx1013DrawTessIndexAuto`, `agcGfx1013SetTessellationRings`, `agcGfx1013BuildTessellationOffchipLayouts`, `agcGfx1013BuildTessellationRingTable`, `sceAgcDriverSetTFRing` (5) | `vulkan_ps5_core.c:7056,7053,2975`; `vulkan_ps5.c:1637,1646` | native `AgcGraphicsPipeline`/device own tessellation rings + offchip layouts |
| **Duplicate compute encoder** | `agcGfx1013ApplyComputeDefaultsV8`, `agcGfx1013DispatchCompute` (2) | `vulkan_ps5_core.c:6351,6376` | `vkCmdDispatch` records only `agcCmdDispatch` |
| **Direct descriptor-table assembly** | `agcGfx1013BufferDescriptorEncode`, `agcGfx1013CombinedImageSamplerDescriptorEncode`, `agcGfx1013Image2DDescriptorEncode`, `agcGfx1013RawBufferDescriptorEncode` (4) | `vulkan_ps5_core.c:6698/6720,6298,6205/6291,5918` | native descriptor sets own encoding (also unblocks `descriptors_ready` for native draw) |
| **ICD-owned image-layout calculators** | `agcGfx1013GetColorSurfaceLayout`, `agcGfx1013GetDepthSurfaceLayout` (2) | `vulkan_ps5_core.c:1653,1692` (`vkCreateImage` MSAA-color / depth) | `AgcImage` owns MSAA-color and depth surface layouts |
| **ICD-owned border-color table** | `agcGfx1013SetBorderColorTable` (1) | `vulkan_ps5_core.c:7524` (per-draw, into `command->dcb`) | native device owns the border-color table |
| **ICD-owned shader fusion** | `sceAgcFuseShaderHalves_0200` (1) | `vulkan_ps5_core.c:3085` (`finalize_runtime_shader`) | `agcCreateShader` owns code alloc + flush + front/back fusion |
| **ICD-owned GPU heap (cross-cutting)** | `agcGpuMemoryAllocateFlexible`, `agcGpuMemoryFlush`, `agcGpuMemoryFreeFlexible` (3) | 9 / 11 / 19 sites across shader code, descriptor tables, vertex tables, submit memory, tess rings, border color | **all** of the above migrate; this is the last symbol to leave |

Total: 5+2+9+5+2+4+2+1+1+3 = 34. ✓

Key structural fact: the `agcGpuMemory*` triple (3 symbols) is shared across
**six** migration clusters, so it cannot leave the inventory until shader code,
descriptor tables, vertex tables, submit memory, tess rings, **and**
border-color memory all move to native ownership. It is the long pole, not a
quick win.

## 2. The four candidate slices, ranked

### Revised recommendation: transfer-image region/buffer forms next. Shader ownership and legacy-submission removal follow.

### A. WSI → `AgcPresentChain` (completed host slice)

- **Direct-call reduction achieved:** 37 → 34. The source audit and focused
  WSI CTest pass with no `agcVideoOut*` use in Vulkan-PS5.
- **Unblocked on OpenAGC:** `agcCreatePresentChain`/`agcDestroyPresentChain`/
  `agcPresent` already exist (`include/openagc/runtime.h:1517-1525`) and are
  FW 5.50 **and** FW 11.60 qualified (OpenAGC STATUS.md lines 236-246: "passes
  registration, initial transition, first flip, round-trip transition, final
  flip, and clean teardown"). The Vulkan-PS5 WSI block is the only remaining
  consumer of the raw `agcVideoOut*` triple.
- **Preserves one ordered native Vulkan queue stream:** presentation is
  out-of-band. Swapchain images transition to `kAgcResourceUsageVideoOutScanout`
  on the graphics stream via the existing `native_transition_whole_image` path,
  then `agcPresent(chain, index, frame_id, ready_fence, timeout)` waits on a
  readiness fence from that same graphics queue. No second queue, no second
  stream.
- **Exact symbols/files to touch:**
  - `src/vulkan_ps5_wsi.c` — replace `agcVideoOutOpen` (line 340),
    `agcVideoOutPresent` (line 503), `agcVideoOutClose` (line 190) with
    `agcCreatePresentChain`/`agcPresent`/`agcDestroyPresentChain`. The
    `AgcVideoOut *video_out` field in `VkPs5Swapchain` (line 22) becomes
    `AgcPresentChain present_chain`.
  - `src/vulkan_ps5_wsi.c` — swapchain image creation (lines 283-330): images
    must carry `AGC_IMAGE_USAGE_SCANOUT_BIT` so `agcCreatePresentChain` accepts
    them. Currently `VK_IMAGE_TILING_LINEAR`, `memoryTypeIndex=1` (line 310) —
    verify that memory type is scanout-capable or move to the scanout-qualified
    heap.
  - `src/vulkan_ps5_wsi.c` — `acquire_next_image` (lines 389-423) and
    `vkQueuePresentKHR` (lines 448-516): replace CPU-side
    `atomic_bool acquired[]` polling with a native readiness fence wired from
    `vkQueueSubmit`. `agcPresent` takes `AgcFence ready_fence`; the current
    `vk_ps5_queue_submit_native` (`src/vulkan_ps5.c:273-306`) creates and
    *waits* on a fence internally and does not return one — so WSI needs either
    a submit variant that returns a signaled fence, or a pre-signaled fence
    given the synchronous-submit model.
- **Tests to add/extend:**
  - `tests/wsi.c` (gate `vulkan_ps5.wsi`, line 136 of `CMakeLists.txt`) — add a
    case asserting the swapchain holds an `AgcPresentChain`, not a raw
    `AgcVideoOut`.
  - `tests/swapchain_runner.sh` (gate `vulkan_ps5.swapchain_runner`, line 171) —
    extend to cover acquire→submit→present with a native readiness fence and
    exhaustion/recreation.
  - `analysis/native_runtime_calls.tsv` — remove the 3 `agcVideoOut*` rows;
    `tests/native_migration_audit.py` will then enforce their absence.
- **Resolved design hazards:**
  - **Readiness-fence plumbing.** Native submission retains its actual
    completion fence after the bounded wait; legacy submission publishes a
    signaled native fence only after its bounded GPU-label wait. Presentation
    and submission serialize on the queue lock and `agcPresent` consumes that
    causally published fence.
  - **Scanout memory type.** Swapchain images use the garlic heap and propagate
    `VkMemoryDedicatedAllocateInfo` to
    `AGC_MEMORY_CREATE_DEDICATED_BIT`; scanout usage is added before querying
    the final native layout.
  - **Retirement/retention.** OpenAGC API v14 added fence-keyed buffer/image
    retirement and present-chain retention (OpenAGC STATUS.md 247-257). The
    swapchain images must remain retained across present; verify
    `agcDestroyPresentChain` releases them on `vkDestroySwapchainKHR`.
  - **Do not** carry over the `frame_id` polling or the
    `wait_for_acquire_progress` nanosleep loop (lines 54-63) — those are
    CPU-side substitutes for fence semantics that the native chain replaces.

### B. Transfer-image region + buffer/image forms (completed host slice)

**Outcome:** `vkCmdCopyImage`, `vkCmdCopyBufferToImage`, and
`vkCmdCopyImageToBuffer` now use OpenAGC API 41 layout-derived region records.
The command-recording gate covers partial color copies and a strided
buffer→image→buffer chain. `vkCmdClearColorImage`, `vkCmdBlitImage`,
`vkCmdClearDepthStencilImage`, `vkCmdClearAttachments`, and
`vkCmdResolveImage` fail closed. The detailed bullets below are retained as
the pre-implementation rationale and hazard checklist; their silent-stub and
missing-copy-contract statements are resolved historical context.

- **Direct-call reduction:** 0 in the short term (new native `agcCmd*` calls
  are not in the audit regex). STATUS.md lines 222-223 already note the
  whole-image slice "adds no direct low-level call." This is correct: the win is
  **correctness and unblocking the PLAN's next step**, not a 37-count drop.
- **Why it cannot be skipped — silent no-op hazard.** Seven transfer/clear
  commands are bare `IGNORE(...)` stubs that set no `record_error` and do **not**
  call `native_mark_stream_incomplete`:
  - `vkCmdCopyBufferToImage` (`vulkan_ps5_core.c:5545-5548`)
  - `vkCmdCopyImageToBuffer` (5550-5553)
  - `vkCmdClearColorImage` (5924-5928)
  - `vkCmdBlitImage` (7828-7831)
  - `vkCmdClearDepthStencilImage` (7833-7837)
  - `vkCmdClearAttachments` (7839-7842)
  - `vkCmdResolveImage` (7844-7847)

  A command buffer containing only these returns `VK_SUCCESS`, stays
  `native_stream_complete=TRUE`, and submits via the native path with **zero
  work recorded** for those commands. This is silent data loss on both streams
  — strictly worse than the fail-closed `VK_ERROR_FEATURE_NOT_PRESENT` used by
  the partial-region `vkCmdCopyImage` path (line 5521). This is the single most
  urgent correctness defect in the migration.
- **OpenAGC gap.** `agcCmdCopyImage` is whole-image only
  (`include/openagc/runtime.h:1593-1598`). There is no
  `agcCmdCopyBufferToImage`/`agcCmdCopyImageToBuffer`/region-`agcCmdCopyImage`/
  `agcCmdBlitImage`/`agcCmdClearColorImage`/`agcCmdResolveImage` yet.
  OpenAGC API 39 now supplies `agcCmdUpdateBuffer` and `agcCmdFillBuffer`,
  and Vulkan records both natively. OpenAGC has
  `agcWriteImage`/`agcReadImage` (runtime.h 1528-1531) for host-side raw byte
  transfer, but no command-buffer transfer contracts. So this slice requires
  **paired OpenAGC runtime work** (add the typed transfer commands) before
  Vulkan-PS5 can consume them.
- **Preserves one ordered native Vulkan queue stream:** all transfer commands
  record on the same graphics `AgcCommandBuffer` that draws dispatch, behind
  typed `CopySource`/`CopyDestination` transitions — exactly the pattern
  already proven by `vkCmdCopyBuffer` (lines 5474-5497) and whole-image
  `vkCmdCopyImage` (lines 5524-5542).
- **Exact symbols/files:**
  - OpenAGC `include/openagc/runtime.h` — add `agcCmdCopyImage` region/
    subresource form, `agcCmdCopyBufferToImage`, `agcCmdCopyImageToBuffer`,
    and at minimum `agcCmdClearColorImage` (blit/resolve can stay fail-closed
    initially).
  - `src/vulkan_ps5_core.c` — replace the seven remaining `IGNORE` stubs with native
    recording or explicit `VK_ERROR_FEATURE_NOT_PRESENT` (fail-closed) until the
    native command exists. **At minimum, the stubs must call
    `native_mark_stream_incomplete` or set `record_error` today** so they stop
    silently dropping work.
  - `tests/command_recording.c` — extend the existing whole-image copy test
    (lines 1034-1044) with region, buffer↔image, and clear/fill positive and
    negative coverage.
- **Hazards:**
  - The stubs currently pass VVL only because VVL flags them as
    unsupported-features; the ICD must not advertise transfer/clear capability
    it silently drops. Either implement or fail-closed — never both leave the
    stub as a quiet `VK_SUCCESS`.
  - Region `agcCmdCopyImage` must reject layout conversion and overlapping
    subresources the same way the whole-image form does (runtime.h 1593-1596
    comment); Vulkan `vkCmdCopyImage` permits format-compatible but
    different-layout copies, which the native contract may not — map
    conservatively and fail-closed.

### C. Shader ownership completion (sequence after A+B)

- **Direct-call reduction:** 1 (`sceAgcFuseShaderHalves_0200`). The shader-code
  `agcGpuMemoryAllocateFlexible`/`Flush`/`FreeFlexible` sites
  (`vulkan_ps5_core.c:3029,3038,3067,3078,2991,2992`) migrate too, but those
  three symbols only leave the inventory when **all** six memory clusters
  migrate, so the visible 37-count drop is just 1.
- **What it requires:** `agcCreateShader` must own code allocation, flush,
  relocation, and front/back half-fusion. Today `finalize_runtime_shader`
  (`vulkan_ps5_core.c:3000-3097`) calls `agcCreateShader` then manually
  `agcGpuMemoryAllocateFlexible`s code, copies, flushes, relocates the record
  (`agcShaderRecordRelocateBinary`), and fuses halves
  (`sceAgcFuseShaderHalves_0200`, line 3085). The `AgcShaderDesc` already
  carries `front_code`/`front_code_size` (lines 3007-3008), so the native side
  has the inputs — it just doesn't take ownership yet.
- **Why not first:** smaller measurable win (1 call), entangled with the
  cross-cutting `agcGpuMemory*` triple, and the shader path is already
  FW-qualified (STATUS.md 126-137). It does not unblock the duplicate-encoder
  deletion the way descriptor encoding does.

### D. Legacy submission + duplicate-encoder removal (sequence last)

- **Direct-call reduction:** the largest — 5 (submit) + 2 (cursor) + 9
  (graphics encoder) + 5 (tess encoder) + 2 (compute encoder) = **23 symbols**,
  plus the `agcGpuMemory*` triple finally leaves once submit/vertex/descriptor/
  tess/border memory all migrate. This is the slice that takes 37 toward single
  digits.
- **Hard dependencies (must precede):**
  1. **Descriptor encoding native** (4 calls) — otherwise `descriptors_ready`
     is false and `vkCmdDispatch`/`record_graphics_draw` call
     `native_mark_stream_incomplete` (`vulkan_ps5_core.c:6397,7622`), forcing
     the legacy path. This is the single biggest blocker for the duplicate
     encoder.
  2. **Tessellation rings native** (5 calls) — `record_tessellation_draw` has
     no native draw equivalent today; it always emits
     `agcGfx1013DrawTessIndexAuto`/`agcGfx1013SetTessellationRings` into
     `command->dcb` (lines 7053-7056) and relies on the device tessellation ring
     setup (`vulkan_ps5.c:1600-1669`). The native `agcCmdDraw` path is used for
     tessellation pipelines (line 7649), but the legacy emission must be
     deleted, which requires the native pipeline/device to own rings.
  3. **Transfer commands (slice B)** — per PLAN line 169-172: "extend the
     native runtime with typed region and buffer/image copy contracts, migrate
     those remaining transfer commands and WSI command forms, **then** delete
     legacy submission/fence handling and the other superseded encoders."
     Transfer + WSI are the documented prerequisites.
- **Why not next:** it is the most entangled slice, blocked on descriptor
  encoding + tessellation + transfer + WSI. Sequencing it now would require
  carrying the duplicate encoder indefinitely.

## 3. Hazards to carry forward

1. **Silent no-op stubs (urgent).** The seven remaining `IGNORE` transfer/clear stubs
   silently drop work on both streams. Until slice B lands, each stub must at
   minimum set `command->record_error = VK_ERROR_FEATURE_NOT_PRESENT` or call
   `native_mark_stream_incomplete` so the command buffer fails closed instead
   of returning `VK_SUCCESS` with no effect.
2. **`agcGpuMemory*` is the long pole.** Three symbols span six clusters; any
   "quick win" that only migrates one cluster (e.g. shader code) will not
   reduce the 37. Plan the memory migration as a coordinated final sweep, not
   piecemeal.
3. **`native_migration_audit.py` is call-site-agnostic.** It only checks symbol
   presence, so partial migration of a multi-site symbol (e.g. removing
   submit-memory `agcGpuMemoryFlush` but leaving descriptor-table
   `agcGpuMemoryFlush`) does not change the count and can mask progress. The
   TSV `native_owner` column ("AgcImage/AgcBuffer" for the memory triple) is
   also misleading — the real owners are six different native objects.
4. **WSI endpoint scope.** ELF
   `0b1d87d02a5fbe480cc74890c613752bb55c2e7b5f4e729413314785e5302888`
   passed the bounded 1,800-frame FW 5.500.008 swapchain gate with clean
   teardown and exact-PID absence. FW 11.60 remains deferred until that
   endpoint is available.
5. **Tessellation native draw gap.** `agcCmdDraw`/`agcCmdDrawIndexed` are used
   for tessellation pipelines (line 7649), but the legacy
   `agcGfx1013DrawTessIndexAuto` is still always emitted (line 7056). Confirm
   the native `AgcGraphicsPipeline` for tessellation actually drives
   tessellation dispatch through `agcCmdDraw` before deleting the legacy
   emission, or the native stream will run a baseline draw with a tessellation
   pipeline bound.
6. **`agcGfx1013TransitionResource` is render-pass-only.** All four sites
   (8070, 8158, 8263, 8309) are the legacy render-pass begin/end color/depth
   transitions into `command->dcb`. The native path already uses
   `agcCmdTransitionResources` via `native_transition_whole_image`. Deleting
   the legacy render-pass frame prologue removes this symbol cleanly — no
   native transition work is lost.

## 4. Bottom line

WSI and transfer-image migration are complete, and the shader-execution slice
has reduced the authoritative inventory to **26 audited direct symbols** while
preserving the single ordered graphics stream. Proceed with descriptor and
tessellation-resource ownership, then delete the legacy encoder and submission
path. The TSV audit must reach exactly zero before migration completion.
