# FW 5.50 packed-format qualification (2026-08-01)

## Scope

This slice consumes OpenAGC API 50 for native RGBA8 SNORM/UINT/SINT,
RGB10A2 UINT, and BGR10A2 UNORM images. Vulkan maps the Eden-facing
`A8B8G8R8_*`, `A2B10G10R10_UINT`, and `A2R10G10B10_UNORM` formats to exact
sampled descriptors, color-target classes, image views, feature queries, and
clear packing. Integer forms do not advertise filtering or blits. The BGR
UNORM form does not advertise storage-image use.

The expanded linear-image probe covers these five forms plus the previous 26
scalar/vector formats. It creates the supported sampled/storage/attachment/
transfer images and views, clears all 31 images in one command buffer, waits
on one fence for at most two seconds, and compares every byte in all 1,984
pixels.

This gate proves feature advertisement, image/view creation, native GPU clear,
and readback. Shader sampling/storage and attachment exports remain separate
hardware gates.

## Artifacts and host gates

- OpenAGC API 50 commit: `2741a95`
- Probe ELF SHA-256:
  `07384ba86e1db7b69b3994be320fe4a35fc05db6eec1773d761aa2a9a66602b8`
- Cleanup ELF SHA-256:
  `9fd6b41cf2ea87989c4217234c6f34c96a1ca5dc482355af1258539db77d4d76`
- Normal suite: 55/55 passed
- ASAN/UBSAN suite: 55/55 passed
- Complete Prospero library/example build: passed
- Guarded command: `PS5_HOST=10.0.1.41
  VULKAN_PS5_PROSPERO_BUILD=build-prospero-api50
  examples/run_fw550_integer_formats.sh`

The runner requires cleanup first, pins and verifies local and uploaded
probe/cleanup hashes, accepts only
`integer_formats: PASS formats=31 pixels=1984 exact-bits`, attributes kernel
records to the exact target PID, and checks PID removal and websrv
responsiveness.

## FW 5.500.008 evidence

The identical ELF passed twice back-to-back:

- `qualification-logs/20260801T155001Z-api50-packed-formats-run1.log`
- `qualification-logs/20260801T155001Z-api50-packed-formats-run1-target.klog`
- `qualification-logs/20260801T155026Z-api50-packed-formats-run1.log`
- `qualification-logs/20260801T155026Z-api50-packed-formats-run1-target.klog`

Both runs produced all 1,984 exact pixels, completed Vulkan teardown,
self-terminated through SystemService, left no matching process, and left
websrv responsive for the immediate relaunch. Each target log contained only
the accepted raw-ELF loader warning of `0x4000` bytes. Neither contained a GPU
fault, timeout, fatal lifecycle event, reset, or kernel panic. The console did
not panic during these runs.

FW 11.60 replay remains deferred to the final identical-artifact endpoint
qualification.
