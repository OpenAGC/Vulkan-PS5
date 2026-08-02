# FW 5.50 Eden descriptor-rollover construction gate (2026-08-02)

## Scope

This gate closes the `VK_ERROR_INITIALIZATION_FAILED` that followed Eden's
`rasterizer-texture-cache` checkpoint after unnormalized samplers and compute
scratch had already been accepted. Its bounded oracle is full construction of
`RasterizerVulkan`; it does not claim game rendering, descriptor execution,
clean teardown, relaunch, or compute-scratch readback.

## Attribution

`BufferCacheRuntime` first constructs its always-enabled `QuadIndexedPass`.
That compute pass uses one `VkDescriptorUpdateTemplateEntry` beginning at
binding 0 with `descriptorCount = 2` across two compatible one-element
`VK_DESCRIPTOR_TYPE_STORAGE_BUFFER` bindings. Vulkan requires the second
descriptor to roll into binding 1.

Vulkan-PS5 instead required the complete entry to fit binding 0, so
`vkCreateDescriptorUpdateTemplate` returned
`VK_ERROR_INITIALIZATION_FAILED` before shader-module creation. The matching
update path would also have targeted binding 0 array element 1 and silently
dropped the second descriptor. The absence of a new PSBC compile after the
texture-cache checkpoint in
`20260802T071153Z-swapchain-run1.log` corroborated this pre-shader call site.

## Public Vulkan correction

Descriptor templates, ordinary writes, and copies now share one numeric
binding cursor. It:

- resolves flattened descriptor slots independently of `pBindings` order;
- skips omitted and explicit zero-count bindings;
- carries `dstArrayElement`/`srcArrayElement` across applicable bindings;
- requires matching descriptor type, shader stages, and immutable-sampler
  presence for every nonzero binding in the span; and
- copies byte-addressed template payloads with `memcpy`, avoiding unaligned C
  structure loads.

The focused regression uses Eden's exact two-storage-buffer shape and also
covers sparse bindings, an explicit zero-count binding, array offsets beyond
the first binding, template/direct/copy execution, unaligned template data,
and incompatible type, stage, immutable-sampler, and out-of-range spans.
Descriptor binding flags remain outside the advertised profile; immutable
sampler execution is a separate unqualified core-Vulkan gap.

## Validation

- Focused descriptor-update test: pass.
- Full generic host CTest: 62/62 pass.
- Fresh targeted ASan/UBSan build and test: pass with no finding.
- Standalone Vulkan-PS5 Prospero build: pass.
- Source-integrated Eden Release O3 Prospero build: pass.
- Final Eden ELF SHA-256:
  `4eae3b998f9a92664d41b86325a62bc8f9d2186a8c592e471ac180038923e490`.
- Cleanup ELF SHA-256:
  `9fd6b41cf2ea87989c4217234c6f34c96a1ca5dc482355af1258539db77d4d76`.
- Sidecar SHA-256:
  `9f85dcac310c0031ca32bd735a8e6a93d04bfb81c9d60aedc3a659b09c2c5e2b`.
- `2048.nro` SHA-256:
  `cd7e7f343830920196590d99c82a9f1ab8a375eeaeb943fa6c671aa68250a20d`.

The cleanup-first FW 5.500.008 replay is recorded in:

- `examples/qualification-logs/20260802T074820Z-swapchain-run1.log`
- `examples/qualification-logs/20260802T074820Z-swapchain-run1-target.klog`

The application log reaches, in order:

- `rasterizer-texture-cache`
- `rasterizer-buffer-cache-runtime`
- `rasterizer-buffer-cache`
- `rasterizer-query-cache-runtime`
- `rasterizer-query-cache`
- `rasterizer-pipeline-cache`
- `rasterizer-accelerate-dma`
- `rasterizer-fence-manager`
- `eden-ps5: INIT CHECKPOINT rasterizer`

This proves all `RasterizerVulkan` members, including the final event member,
were constructed with the exact pinned candidate. The guarded runner emitted
its scoped PASS for that oracle, then correctly returned failure because a
later `std::__1::system_error` (`thread constructor failed: Resource
temporarily unavailable`) entered the coredump path after SDL audio-device
initialization failed. That frontend/thread-resource failure is the next Eden
slice and is not attributed to Vulkan construction.
