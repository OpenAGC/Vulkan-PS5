# SDL/Zink compatibility profile

This profile follows `../SDL/docs/README-ps5-openagc.md`: Vulkan-PS5 must pass
a pinned Mesa/Zink capability probe before SDL selects Zink for accelerated
OpenGL contexts. OSMesa remains the software fallback, and the existing direct
SDL ps5agc renderer is a separate path.

Pinned inputs:

- Mesa: `44e18d3d7783c751fd77aeba01bbff28db97945a`
- Vulkan-Samples: `89dd3af22d41f9244eeab6e0650460112285c0e1`
- vulkan-more-samples: `17e47ab93a0b04d0bd9a0416dceaa89541b082d1`

`vulkan_ps5_zink_profile_test` evaluates Mesa's GL 2.1 Zink profile against a
live Vulkan-PS5 physical device. Its default reporting mode keeps CTest useful
while work remains; `--strict` returns failure until every required API,
extension, and feature is available. The current report is:

```text
zink-profile: mesa=44e18d3d7783c751fd77aeba01bbff28db97945a api=0 extensions=0 features=0 total=0
```

This slice adds complete host-tested contracts for:

- `VK_KHR_maintenance1`, `VK_KHR_create_renderpass2`, and
  `VK_KHR_descriptor_update_template`;
- `VK_KHR_timeline_semaphore`, including host and queue wait/signal values and
  finite waits;
- `VK_KHR_image_format_list`, `VK_KHR_swapchain_mutable_format`, and
  `VK_KHR_incremental_present` for the qualified BGRA8 SRGB/UNORM WSI pair;
- rectangular `VK_EXT_line_rasterization` without stippling; and
- render-pass-2 command aliases, descriptor-template updates, mutable views,
  present-region validation, and feature/property query chains.

The completed closure adds `VK_EXT_scalar_block_layout`, core `alphaToOne`,
`VK_KHR_dynamic_rendering`, custom border colors with image-view swizzle,
maintenance5 entry points, and Vulkan 1.2 advertisement. The pinned strict
capability probe has no remaining gaps.

`VK_EXT_depth_clip_enable` is now part of that live host contract as well. The
ICD reports and accepts its feature structure, consumes the static graphics
pipeline state, and translates explicit enable/disable to OpenAGC API 45 while
keeping `depthClampEnable` independent. Host tests cover extension enumeration,
feature-chain enablement, reserved-flag rejection, and the exact native flags.
The cleanup-guarded depth probe and warning-free Mesa/Zink FW 5.50 replay are
still required before this addition is hardware-qualified.

FW 5.500.008 qualification for this slice used the refreshed Prospero PSBC
archive. The earlier pipeline failure was an API-version mismatch in a stale
archive and occurred before command submission. The rebuilt candidate passed
the rectangular wide-line oracle exactly:

```text
wide_lines: PASS width8=1024 width16=2048 width32=4096 center=ff0000ff
```

Evidence is retained in
`examples/qualification-logs/20260731T065623Z-wide-lines-run1.log` and
`20260731T065623Z-wide-lines-run1-target.klog`. The wide-line ELF SHA-256 is
`096ddfbc00ed6eab5c4f761ce6b506291736e97e800a8ed5703328d829acc2d5`.
The process self-terminated and the target-only klog contained no fatal event
or warning beyond the established raw-ELF `amount=0x4000` baseline.

The same FW 5.500.008 endpoint then passed the two new cleanup-guarded
readback gates:

```text
scalar_block_layout: PASS stride=12 result_offset=24 result=651a5a5a
alpha_to_one: PASS 18432 green pixels
```

The scalar probe's decoy and guard words distinguish the qualified byte-12
and byte-24 scalar offsets from std430-style placement. The alpha probe's
fragment shader emits alpha 0.25, so an opaque-green readback proves the
alpha-to-one epilog rather than ordinary fragment output. Their respective
ELF SHA-256 values are
`c46f0cf46128c3460b44255b601636cbd591a1cbe513b94e6454f265783045fa` and
`7c07a902fd7a50cc158a2d5430100b3c5df5c3c25e3127b1805b9bee7b74143d`.
Evidence is retained at timestamps `20260731T072748Z` and
`20260731T072824Z` under `examples/qualification-logs/`; both exact PIDs were
absent after self-termination and each target-only klog contained only the
established raw-ELF warning.

FW 5.500.008 also passed render-pass-less dynamic rendering with exactly
18,432 green pixels, then passed a custom-border probe that sampled an
out-of-range opaque-red entry through an R/B-swizzled image view and returned
exactly 18,432 blue pixels. The latter replay used maintenance5
`vkCmdBindIndexBuffer2KHR`. Their ELF SHA-256 values are
`0bb13e7f34a45bf0b8a5cc06779cabc687e85bbb1b4ca6e358fb0c5cf58c26bd` and
`dbf161fbc8e77287cbdfb0254170c8275c9831ef4208e67b215cc38e7a7265d2`;
evidence timestamps are `20260731T074115Z` and `20260731T075944Z`.

This closes the capability profile, not the Zink execution gate. SDL must
retain OSMesa until the pinned Mesa build runs through the PS5 EGL/WSI bridge.

## Execution-gate follow-up (2026-08-01)

The first complete compiler pass on FW 5.500.008 reached Zink's draw path and
then made a null indirect call. The fatal register tuple was the exact SysV ABI
shape of `vkCmdBindVertexBuffers2(command, 0, 1, buffers, offsets, NULL,
NULL)`. The Vulkan 1.2 ICD exported `vkCmdBindVertexBuffers` but neither the
promoted core name nor `vkCmdBindVertexBuffers2EXT`.

Vulkan-PS5 now exposes both proc names for the compatible null-stride form and
performs the same typed buffer, memory, usage, offset, and optional-size bounds
validation as the original command. A non-null stride array records
`VK_ERROR_FEATURE_NOT_PRESENT`; this avoids pretending that the rest of
`VK_EXT_extended_dynamic_state` exists. The generic and sanitizer suites each
pass 46/46. A subsequent guarded attempt stopped before upload because WebSrv
and FTP were not listening, so no new hardware result or hash is claimed.
