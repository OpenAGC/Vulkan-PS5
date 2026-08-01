# FW 5.50 scalar/vector format qualification (2026-08-01)

## Scope

The expanded bounded format probe covers all eighteen formats introduced by
the two Eden uncompressed slices. In addition to the four RGBA16/32 integer
formats previously qualified, it covers OpenAGC API 48's fourteen native
scalar/vector forms:

- R/RG 16-bit UNORM, SNORM, UINT, and SINT
- RGBA16 UNORM and SNORM
- R/RG 32-bit UINT and SINT

For every format, the probe requires the exact advertised linear feature mask,
creates an image with sampled, storage, color-attachment, and transfer usage,
binds host-visible memory, and creates a native image view. Normalized formats
must additionally report linear filtering and source/destination blit support;
integer formats must not report those bits. One command buffer clears all
eighteen images, releases them for host access, and waits on one fence with a
two-second upper bound. Readback checks every component byte in all 1,152
pixels, including normalized boundary/rounding patterns and signed integer
bit patterns.

This gate proves exact format advertisement, image/view creation, and GPU
clear/readback. Shader sampling, storage-image shader operations, and
scalar/vector color-attachment exports remain separate hardware gates.

## Artifact and runner

- OpenAGC commit: `7095b16`
- Vulkan-PS5 implementation commit: `19b6269`
- Probe ELF SHA-256:
  `ec8527214b1681525ec7eb92ab5c24f4f05dfa1fbe027c1f9781415f0853a827`
- Cleanup ELF SHA-256:
  `9fd6b41cf2ea87989c4217234c6f34c96a1ca5dc482355af1258539db77d4d76`
- Guarded command:
  `PS5_HOST=10.0.1.41 VULKAN_PS5_LIVE_KLOG=1 examples/run_fw550_integer_formats.sh`

The runner requires cleanup first, accepts only
`integer_formats: PASS formats=18 pixels=1152 exact-bits`, checks target PID
removal and websrv responsiveness, and rejects fatal or unexpected target
kernel-log records.

## FW 5.500.008 evidence

The identical ELF passed twice back-to-back:

- `qualification-logs/20260801T142420Z-scalar-vector-formats-run1.log`
- `qualification-logs/20260801T142420Z-scalar-vector-formats-run1-target.klog`
- `qualification-logs/20260801T142435Z-scalar-vector-formats-run1.log`
- `qualification-logs/20260801T142435Z-scalar-vector-formats-run1-target.klog`

Both runs produced all 1,152 exact pixels, completed normal Vulkan teardown,
self-terminated through SystemService, left no matching process, and left
websrv responsive for the immediate relaunch. Each target log contained only
the accepted raw-ELF loader warning of `0x4000` bytes; neither contained a GPU
fault, timeout, fatal lifecycle event, or unexpected kernel warning.

FW 11.60 replay remains deferred to the final identical-artifact endpoint
qualification.
