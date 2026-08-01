# FW 5.50 Vulkan depth/stencil image-clear qualification — 2026-08-02

The general `vkCmdClearDepthStencilImage` path records public OpenAGC
meta-compute commands after validating image usage, layout, aspect masks,
normalized depth, selected subresource ranges, and independently queried
depth/stencil plane layouts. The hardware oracle creates linear 8x8 D16, D32,
S8, D16+S8, and D32+S8 images, clears depth to exact `0.25` and stencil to
`0x5a`, then verifies every logical texel through Vulkan subresource layouts.

The qualification also found and fixed a general image-creation defect:
linear depth/stencil images were accepted but were not marked as native depth
surfaces. Consequently their first valid depth transition failed closed and
their memory/native-usage classification was incorrect. Depth-surface
classification now applies to every supported depth/stencil image regardless
of tiling.

Artifacts:

- Probe ELF SHA-256:
  `3085323289d630eee362172c23f305903de5b8ad34243f4c2aa664a62f48d174`.
- Cleanup ELF SHA-256:
  `9fd6b41cf2ea87989c4217234c6f34c96a1ca5dc482355af1258539db77d4d76`.
- Run 1:
  `examples/qualification-logs/20260801T165707Z-depth-clear-run1.log`.
- Run 2:
  `examples/qualification-logs/20260801T165729Z-depth-clear-run1.log`.

Both guarded FW 5.500.008 runs reported
`depth_clear: PASS formats=5 depth=256 stencil=192 exact-bits`. The identical
probe bytes ran after the required cleanup artifact, completed a bounded
two-second fence wait, destroyed all Vulkan objects, self-exited, left the
exact PID absent, and immediately relaunched. Target-attributed klogs contain
no panic, reset, timeout, watchdog, or GPU fault. The only diagnostic is the
accepted raw-ELF `0x4000` baseline VM warning.

This evidence qualifies the exact command path on FW 5.50. It does not yet
qualify depth/stencil attachment-clear pixels, multisampled depth/stencil
clears, D24 formats, or the identical FW 11.60 replay.
