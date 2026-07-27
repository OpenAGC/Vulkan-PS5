# Standalone Vulkan triangle readback

This application uses only standard Vulkan 1.1 APIs. It creates a mapped
256x256 linear RGBA8 color attachment, records a render pass and a three-vertex
draw, waits on a Vulkan fence, invalidates the allocation, and checks both the
solid-green triangle and untouched background pixels.

Build it with `-DVULKAN_PS5_BUILD_EXAMPLES=ON`. The generic host backend
compiles and records the complete command stream but does not execute shaders,
so host execution intentionally reports a readback mismatch. A successful FW
5.50 run prints:

```text
triangle: PASS <count> green pixels
```

The Prospero link uses the same `ps5-payload-libcxx` and
`ps5-payload-openlibm` runtime dependencies as the compute sample.
