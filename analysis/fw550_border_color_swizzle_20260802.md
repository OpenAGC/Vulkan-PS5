# FW 5.50 border-color swizzle qualification — 2026-08-02

## Result

`VK_EXT_border_color_swizzle` is host- and FW 5.50-qualified. Vulkan-PS5 now
advertises both `borderColorSwizzle` and `borderColorSwizzleFromImage`, accepts
`VkSamplerBorderColorComponentMappingCreateInfoEXT` in the sampler chain, and
validates its component selectors and Boolean sRGB declaration before native
sampler creation.

The FW 5.50 probe combines a red custom border with matching B↔R sampler and
image-view mappings. Both cleanup-first runs returned:

```
custom_border_color: PASS covered=18432 blue=18432 swizzle=BR
```

The qualified ELF SHA-256 is
`8e7ed28d20788293fce2a85a5e17072eb557a872efe2f14627620f5d2105012c`.
The cleanup prerequisite SHA-256 is
`9fd6b41cf2ea87989c4217234c6f34c96a1ca5dc482355af1258539db77d4d76`.

## Native mapping

OpenAGC image views already apply the public component mapping to every
sampled value, including an opaque-black or custom border value. Vulkan-PS5
therefore retains the application-supplied custom border in the native sampler
and passes the image-view mapping through the existing public OpenAGC view
descriptor. The explicit sampler mapping declares the matching view contract;
it does not expose or require PM4, raw SGPRs, or raw GPU addresses.

## Verification

- Normal host suite: 61/61 passed sequentially.
- ASan/UBSan suite: 61/61 passed sequentially.
- Prospero custom-border probe built with no new warnings.
- Pinned Vulkan CTS `vulkan-cts-1.4.6.1` commit
  `5c8aae22885448d70a2873e94a93b24b49505c32`:
  `dEQP-VK.info.device_mandatory_features` passes, and the complete
  `dEQP-VK.info.*` group reports 19 pass, zero fail, and two expected
  `NotSupported` cases.
- Corrected ELF ran twice with local and uploaded hash verification, cleanup
  first, finite websrv waits, exact pixel counting, self-exit, attributed klog,
  process-absence verification, and immediate relaunch.
- Logs:
  `examples/qualification-logs/20260802T011339Z-custom-border-color-run1.log`
  and
  `examples/qualification-logs/20260802T011358Z-custom-border-color-run1.log`,
  with matching `-target.klog` files.
- Each run contained only the already-qualified one-time 0x4000 VM warning.

The identical-byte FW 11.60 replay remains part of the final endpoint gate.
