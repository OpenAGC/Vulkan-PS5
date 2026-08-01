# FW 5.50 RGBA integer-format qualification (2026-08-01)

## Scope

The bounded `vulkan_ps5_integer_formats_probe` exercises the four Eden-required
native integer image formats added by commit `293663a`:

- `VK_FORMAT_R16G16B16A16_UINT`
- `VK_FORMAT_R16G16B16A16_SINT`
- `VK_FORMAT_R32G32B32A32_UINT`
- `VK_FORMAT_R32G32B32A32_SINT`

For each format it requires the advertised linear sampled, storage,
color-attachment, and transfer feature mask, creates one image with that union
of usages, binds host-visible memory, and creates a native 2D image view. One
command buffer transitions all four images, performs exact signed or unsigned
`vkCmdClearColorImage` operations, releases the results for host access, and
waits on one fence with a two-second upper bound. Readback compares every byte
of every component in all 256 pixels. Linear filtering, blit, and texel-buffer
features remain unadvertised.

This gate proves integer image creation/view encoding and clear/readback
execution. It does not yet claim shader sampling, storage-image shader access,
or integer color-attachment export execution; those remain separate gates.

## Artifact and runner

- Vulkan-PS5 source commit before the probe slice: `293663a`
- Probe ELF SHA-256:
  `85ed7b3d39f36573cf64f34498bcc4bdaa472c3cc3a8c63c6c3f1789b8c96fff`
- Cleanup ELF SHA-256:
  `9fd6b41cf2ea87989c4217234c6f34c96a1ca5dc482355af1258539db77d4d76`
- Guarded command:
  `PS5_HOST=10.0.1.41 VULKAN_PS5_LIVE_KLOG=1 examples/run_fw550_integer_formats.sh`

The runner requires the cleanup artifact, uploads the exact ELF, accepts only
`integer_formats: PASS formats=4 pixels=256 exact-bits`, enforces target PID
removal, checks websrv responsiveness, and rejects fatal or unexpected target
kernel-log records.

## FW 5.500.008 evidence

The identical ELF passed twice back-to-back:

- `qualification-logs/20260801T140151Z-integer-formats-run1.log`
- `qualification-logs/20260801T140151Z-integer-formats-run1-target.klog`
- `qualification-logs/20260801T140206Z-integer-formats-run1.log`
- `qualification-logs/20260801T140206Z-integer-formats-run1-target.klog`

Both runs produced all 256 exact pixels, completed normal Vulkan teardown,
self-terminated through SystemService, left no matching process, and left
websrv responsive for the immediate relaunch. Each target log contained only
the accepted raw-ELF loader warning of `0x4000` bytes; neither contained a GPU
fault, timeout, fatal lifecycle event, or unexpected kernel warning.

FW 11.60 replay is deferred to the final identical-artifact endpoint pass.
