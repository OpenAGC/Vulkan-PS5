# FW 5.50 BC1-BC7 sampling qualification (2026-08-01)

## Scope

The bounded `vulkan_ps5_bc_sampling_probe` validates shader sampling for all
fourteen advertised BC1-BC7 Vulkan formats. A host generator uses the Mesa
codec sources bundled with `openagc-psbc` to commit one deterministic 4x4
compressed block and its decoded reference value for each UNORM, SNORM, SRGB,
UFLOAT, and SFLOAT encoding. `tools/regenerate_bc_probe_assets.sh --check`
proves that regeneration reproduces the committed header.

The Prospero probe creates mapped linear images, copies each exact compressed
block into its queried subresource layout, creates an image view and sampler,
and updates a distinct combined-image-sampler descriptor set per format. One
compute shader samples the center texel with nearest filtering and writes its
four float components to a mapped storage buffer. Submission uses one Vulkan
fence with a two-second upper bound. Results are compared against the committed
Mesa decode with small class-specific tolerances for hardware conversion and
SRGB quantization.

This gate proves Vulkan format advertisement, compressed layout upload,
sampler/combined-image-sampler descriptor translation, OpenAGC image-view
encoding, and real gfx1013 BC decoding. It does not prove BC image copies,
mip copies, cube sampling, or filtered sampling.

## Discovery and correction

The first five-format smoke run failed safely at BC5: hardware returned
`(R,G,R,G)` instead of Vulkan's required `(R,G,0,1)`. OpenAGC had left SQ's
physical `XYZW` selectors in every sampled descriptor. OpenAGC commit
`3bf5b22` now explicitly selects `(X,0,0,1)`, `(X,Y,0,1)`, or `(X,Y,Z,1)` for
one-, two-, and three-channel formats. The corrected smoke ELF then passed.

The first full run passed through BC7 UNORM and found the expected gfx1013 SRGB
conversion differed from Mesa's reference by less than one SRGB quantization
step. The oracle was narrowed to a 1/1024 SRGB tolerance; the driver and shader
bytes were unchanged. Neither discovery failure was a console panic: both
failed closed, tore down, left no matching process, and left websrv responsive.

## Artifacts and runners

- OpenAGC selector-fix commit: `3bf5b22`
- Vulkan-PS5 implementation commit: `ab6a269`
- Full probe ELF SHA-256:
  `601d0d2694c819e48140b429bb9e16b473ea91b5c9ad9eaac69bb8ae8624b639`
- Smoke probe ELF SHA-256:
  `5ac66e2a08cf9da77b64b863d0ce15dd0fded16522f6410150d167ff767abe6d`
- Cleanup ELF SHA-256:
  `9fd6b41cf2ea87989c4217234c6f34c96a1ca5dc482355af1258539db77d4d76`
- Full runner:
  `PS5_HOST=10.0.1.41 VULKAN_PS5_LIVE_KLOG=1 examples/run_fw550_bc_sampling.sh`

The shared guarded runner verifies the local ELF hashes, uploads cleanup and
probe artifacts, downloads the uploaded bytes and verifies their hashes again,
runs cleanup first, requires the exact PASS verdict, checks exact-PID removal,
and verifies websrv responsiveness. Unexpected target kernel records fail the
run; the known raw-ELF loader allocation warning is narrowly accepted.

## FW 5.500.008 evidence

The identical final ELF passed twice back-to-back:

- `qualification-logs/20260801T145318Z-bc-sampling-run1.log`
- `qualification-logs/20260801T145318Z-bc-sampling-run1-target.klog`
- `qualification-logs/20260801T145332Z-bc-sampling-run1.log`
- `qualification-logs/20260801T145332Z-bc-sampling-run1-target.klog`

Both runs printed `bc_sampling: PASS formats=14 sampled=14 exact-blocks`,
completed normal Vulkan teardown and SystemService self-exit, left no matching
process, and permitted immediate relaunch. The target logs contained only the
accepted raw-loader `0x4000` warning and no GPU fault, timeout, fatal lifecycle
event, or unexpected kernel warning.

FW 11.60 replay remains deferred to the final identical-candidate endpoint
qualification.
