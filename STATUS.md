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

## Milestone 3: OpenAGC command emission (indexed/textured qualified)

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
  verifies D32 transitions and exact surface/control PM4 emission.
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
  constants, depth/stencil clears, MRT, and general
  dynamic raster state are not emitted yet.
- On FW `0x05500008`, `examples/run_fw550_m3.sh` completed two consecutive
  compute runs with `PASS 1024 deterministic values` and two consecutive
  triangle runs with `PASS 18432 green pixels`. Two indexed-textured runs each
  reported `PASS 18432 pixels 64+ colors`. This additionally qualifies indexed
  vertex fetch, fragment sampled-image/sampler tables, bilinear filtering, the
  256-byte linear texture row pitch, and mapped render-target readback.
- `examples/run_fw550_m3.sh` remains the authoritative regression gate: it runs
  all three Prospero samples twice through foreground websrv, rejects a missing
  PASS oracle, and retains per-run output without committing runtime logs.
