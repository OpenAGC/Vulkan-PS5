# Implementation Status

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
  enable the corresponding optional Vulkan features before shader qualification.

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

- Geometry and tessellation feature advertisement before repeated hardware
  qualification.
- Sparse and protected resources, external handles, multiview, YCbCr conversion,
  timeline semaphores, descriptor indexing, and VideoOut WSI.

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
  hardware-qualified; the core feature remains disabled until the standard
  feature-request path is enabled and rerun.
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
  scope. `tessellationShader` remains false until patch-output reads are fixed
  and hardware-qualified.
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
- `vkCmdBindVertexBuffers`, `vkCmdBindIndexBuffer`, and `vkCmdDrawIndexed` now
  retain ordinary Vulkan binding state, encode each pipeline binding into a
  per-draw GPU-visible gfx1013 vertex table, patch the compiler-selected table
  SGPR, and emit OpenAGC's hardware-qualified `DRAW_INDEX_2` path for UINT16
  and UINT32 indices. Signed vertex offsets and first-instance values use the
  compiler metadata-selected SGPRs.
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
  attachments with matching full-component, blend-disabled state. Begin/end
  transitions cover every attachment, OpenAGC binds CB0-CB7, the target mask
  enables every active slot, and fragment export context carries the real MRT
  count. The command regression verifies CB1 and dual RGBA8 export `0x44`.
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
  an active pass and unsupported clear/dynamic/multisample/blend state fail
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
  to the repeated Milestone 3 regression suite. The staged probes remain for
  isolating lifecycle, reset, idle-ZPASS, and live-counting regressions.
