# FW 5.50 packed 8/16-bit format qualification (2026-08-01)

## Scope

This slice consumes OpenAGC API 51 for `R5G6B5`, `B5G6R5`, `R5G5B5A1`,
`A1R5G5B5`, `A4B4G4R4`, and `R4G4` UNORM images. Vulkan exposes exact
sampled descriptors, clear packing, and format properties. The five 16-bit
forms are filterable, transferable, blit-capable color attachments without
storage or texel-buffer claims. R4G4 is sampled/filterable and transferable,
with source blit only because gfx10.3 has no matching color-target encoding.

The expanded linear-image probe creates every supported image and view,
clears all 37 formats in one command buffer, waits on one fence for at most
two seconds, and compares every byte in all 2,368 pixels. This gate proves
format advertisement, image/view creation, native GPU clear, and readback;
shader sampling and attachment exports remain separate gates.

## Artifacts and host gates

- OpenAGC API 51 commit: `7636d61`
- Probe ELF SHA-256:
  `0a5b5f4a89d2a2b52dd54e935d8c7385215197b22fdbad63dd0fd5287b12f07d`
- Cleanup ELF SHA-256:
  `05a361ef5acec0f9207249c516be75906bbd31b698655555c1785724ca28f9a6`
- Clean normal suite: 55/55 passed
- Clean ASAN/UBSAN suite: 55/55 passed
- Complete Prospero library/example build: passed
- Guarded command: `PS5_HOST=10.0.1.41
  VULKAN_PS5_PROSPERO_BUILD=build-prospero-api51
  examples/run_fw550_integer_formats.sh`

The runner requires cleanup first, pins and verifies local and uploaded ELF
hashes, accepts only
`integer_formats: PASS formats=37 pixels=2368 exact-bits`, attributes kernel
records to the exact target PID, and checks PID removal and websrv
responsiveness.

## FW 5.500.008 evidence

The identical ELF passed twice back-to-back:

- `examples/qualification-logs/20260801T162250Z-api51-packed16-formats-run1.log`
- `examples/qualification-logs/20260801T162250Z-api51-packed16-formats-run1-target.klog`
- `examples/qualification-logs/20260801T162311Z-api51-packed16-formats-run1.log`
- `examples/qualification-logs/20260801T162311Z-api51-packed16-formats-run1-target.klog`

Both runs produced all 2,368 exact pixels, completed Vulkan teardown,
self-terminated through SystemService, left no matching process, and left
websrv responsive for the immediate relaunch. Each target log contained only
the accepted raw-ELF loader warning of `0x4000` bytes. Neither run contained a
GPU fault, timeout, fatal lifecycle event, reset, or kernel panic. The console
did not panic during these runs.

FW 11.60 replay remains deferred to the final identical-artifact endpoint
qualification.
