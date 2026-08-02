# FW 5.50 Eden unnormalized-sampler gate

## Scope

This slice closes the `VK_ERROR_FEATURE_NOT_PRESENT` raised while Eden revision
`612409c7ba` constructed `Vulkan::BlitImageHelper`. It adds a general Vulkan
sampler translation through the public OpenAGC API; there is no Eden-specific
driver branch or direct hardware-descriptor access.

## Attribution

The failing FW 5.50 diagnostic
`20260802T053807Z-swapchain-run1.log` completed device, VMA, scheduler,
swapchain, presentation, staging-pool, descriptor-pool, and both descriptor
queue construction. Its final marker was
`rasterizer-compute-pass-descriptor-queue`; the next member was
`BlitImageHelper`.

That constructor creates a linear and then a nearest sampler with
`unnormalizedCoordinates = VK_TRUE`. The ICD rejected that bit before calling
OpenAGC. Descriptor-set layouts, shader modules, and descriptor allocation
cannot return `VK_ERROR_FEATURE_NOT_PRESENT` on this path, while the pipeline
layouts use supported combined-image-sampler bindings and push-constant sizes.

## Implementation and host evidence

Vulkan-PS5 validates the Vulkan unnormalized-coordinate restrictions before
setting OpenAGC's typed `AGC_SAMPLER_UNNORMALIZED_COORDINATES_BIT`. The
`vulkan_ps5.command_recording` test creates Eden's exact linear and nearest
samplers. It also verifies fail-closed rejection for mismatched filters, linear
mip filtering, nonzero LOD, repeat addressing, anisotropy, and comparison.

The complete generic Vulkan-PS5 suite passes 61/61, the complete OpenAGC suite
passes 19/19, and both projects build cleanly for Prospero. OpenAGC separately
checks the exact native `FORCE_UNNORMALIZED` descriptor bit and rejects unknown
public sampler flags.

## Hardware result

The cleanup-first FW 5.500.008 run used Eden ELF SHA-256
`1d7bdec9a08caf24a23b39fbbac8ec1aefb4ea1a4bd18798e88078d55b18a21f`.
`20260802T061452Z-swapchain-run1.log` records both the `rasterizer-blit-image`
and `rasterizer-render-pass-cache` markers, proving that both sampler objects
and the complete helper constructed. The former
`VK_ERROR_FEATURE_NOT_PRESENT` is therefore closed.

The same run then failed closed while creating a separate production compute
pipeline: wave32, 36,864 bytes of scratch, zero LDS, and an 8x8x1 workgroup.
That is the next capability slice. This sampler gate does not claim a sampled
pixel oracle, full Eden rendering, clean failed-initialization teardown, or
Vulkan conformance.
