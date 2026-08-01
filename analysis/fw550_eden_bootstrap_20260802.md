# FW 5.50 Eden Vulkan bootstrap qualification — 2026-08-02

The first Eden-owned Prospero executable now creates a standard Vulkan
instance, PS5 surface, device, three-image FIFO swapchain, command buffers,
and synchronization objects; clears and presents 600 frames; destroys the
complete lifecycle; and self-exits through the system-service path. Eden uses
only Vulkan entrypoints plus the PS5 system-exit service. OpenAGC and VideoOut
remain behind Vulkan-PS5.

## Failures isolated

The first Release build produced a user-process SIGSEGV while creating the
swapchain. Kernel logs showed a null write and a corrupted return path whose
instruction pointer landed inside archived PSBC code; there was no kernel
panic. An optimization matrix established:

- all components at O0 passed initialization but exposed a later resource
  transition mismatch;
- Eden and Vulkan-PS5 at O3 with OpenAGC at O0 passed 600 frames;
- compiling only `OpenAGC/src/videoout_prospero.c` at O0 also passed;
- compiling only the linear-registration patch helper at O0 still crashed;
- compiling only `agcVideoOutOpen` at O0 passed.

The installed FW 5.50 VideoOut attribute setter was being called with the
nominal 0x20-byte stack record. OpenAGC now constructs the hardware-proven
legacy fields directly in a zeroed 0x40-byte carrier. With every component
restored to Release O3, the crash disappeared.

The O0 run also exposed a Vulkan command-buffer reuse bug. The hidden buffer
alias used by color/depth meta clears ended each submission in `CopySource`,
while the pre-recorded command retained its first-use `Undefined` transition.
On frame-slot reuse OpenAGC correctly rejected that stale precondition.
Vulkan-PS5 now restores the alias buffer to the state captured before command
recording. Host coverage submits both color and depth/stencil clear command
buffers twice.

## Verification

- Fresh OpenAGC generic configure/build: 19/19 CTest suites and
  `19544 passed, 0 failed` (the worktree includes the separately pending
  reference-game tests).
- Clean OpenAGC Release Prospero build: pass, warnings clean.
- Fresh Vulkan-PS5 configure/build: pass; 59/59 CTest suites.
- Focused reusable-clear regression: two consecutive submissions pass for
  both color and depth/stencil.
- O0 control after the alias-state fix: 600/600 frames, teardown, process
  absence, and websrv recovery.
- Final Release O3 ELF SHA-256:
  `3e07642449b6dddd371cb233bddb88a62a70a50a15efb20f43f028493591fa9e`.
- The exact Release bytes passed twice on FW 5.500.008, including an immediate
  relaunch. Both runs reported `PASS 600 frames`, bounded waits, clean
  teardown, exact PID absence, and only the established 0x4000 baseline VM
  warning.
- Evidence logs:
  `20260801T195551Z-eden-ps5-vulkan-bootstrap-release-attr-fix` and
  `20260801T195905Z-eden-ps5-vulkan-bootstrap-release-attr-fix-relaunch` in
  Eden's local `build-prospero-bootstrap/qualification-logs` directory.

FW 11.60 replay remains deferred until the final Vulkan/Eden qualification
sequence, as requested. Visible presentation still needs the user's display
confirmation; the runner proves submission, presentation returns, teardown,
and relaunch but cannot inspect the television output.

## Production quad-index compute follow-up

The next Eden-owned gate compiles
`src/video_core/host_shaders/vulkan_quad_indexed.comp`, uses Eden's pinned
VMA implementation for its input/output/readback buffers, binds both reflected
storage descriptors and the 12-byte push-constant range, and round-trips a
Vulkan pipeline-cache header before recreating the compute pipeline.

The first FW 5.50 attempt failed closed at command recording because partial
buffer barriers deliberately invalidate Vulkan-PS5's coarse whole-buffer
state mirror while descriptor preparation still consulted that mirror. The
driver now queries the exact native command-buffer range represented by each
descriptor. The focused partial-range regression and the full 59-test generic
suite pass. A second FW 5.50 attempt proved that the corrected command reached
native submission, then returned a deterministic readback mismatch: the
1024-thread production shader left the pre-dispatch VMA pattern unchanged.
This was neither reported nor treated as a kernel panic.

Investigation found that OpenAGC still emits a hardcoded all-ones
`COMPUTE_RESOURCE_LIMITS` value, whereas gfx10 requires wave-count-derived
SIMD distribution for this 32-wave workgroup. That separate OpenAGC correction
is host-tested but remains hardware-unqualified and therefore is not part of
the completed Vulkan descriptor-range slice.
