# Implementation Status

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
self-exited, and left only the known single `amount=0x4000` warning. The Eden
profile now has two feature gaps: `imageCubeArray` and `multiViewport`.

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

- Sparse and protected resources, external handles, multiview, YCbCr conversion,
  timeline semaphores, and descriptor indexing.

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
  plus transitive `-lkernel`, `-lSceAgcDriver`, `-lSceVideoOut`, `-lunwind`,
  `-lc++abi`, `-lc++`, and `-lm`. `SceSystemService` remains an explicit
  application lifecycle dependency rather than an ICD dependency.
- The normal Prospero driver/examples build remains clean after the package
  test was added. The retained installed-package ELF has SHA-256
  `3da3698026eb62d5a97aedb8aa806ee0c6bc18469aa053ac32cc7caa16deb635`.
- `run_fw550_package_consumer.sh` reuses the proven bounded raw-ELF lifecycle
  gate with a package-specific ELF and PASS oracle. Its simulation verifies
  exact-PID handling and acceptance of only the proven single `0x4000`
  baseline warning.
- One bounded FW 5.500.008 execution after a fresh console-availability signal
  passed without retry. PID 153 printed `package-consumer: PASS result=0`,
  completed self-requested KillApp followed by `All processes exited`, was
  absent in the exact-PID ps5debug-NG check, and left websrv responsive. The
  scoped klog contained no fatal event or warning beyond the proven raw-ELF
  `amount:0x4000` baseline. Evidence:
  `20260728T070752Z-package-consumer.log` and
  `20260728T070752Z-package-consumer-target.klog`.

This closes Milestone 5 at the reusable static SDK/package scope.

## Milestone 6: Eden compatibility profile (in progress)

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
  requests the feature and flag normally. The live profile is now two
  extension plus 25 feature gaps, total 27.
- Runtime audit also identifies missing Eden VMA-pattern coverage, a 19-format
  subset with no BC/D24/storage-image support, and the still-missing Prospero
  surface/build/static-entrypoint integration in Eden itself.
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
