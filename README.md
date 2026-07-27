# Vulkan-PS5

Vulkan-PS5 is an application-neutral Vulkan ICD for PlayStation 5 homebrew. The
current implementation is the host-testable Milestone 1 ICD: it exposes the
complete Vulkan 1.0/1.1 core entrypoint surface, conservative gfx1013 physical
device properties, two PS5 memory classes, host-backed resources and
synchronization objects, loader dispatch, a static SDK library, and a
loader-compatible shared library. Features which do not yet have verified
OpenAGC hardware implementations are deliberately not advertised.

The ICD consumes OpenAGC's application-neutral gfx1013 capability query for
qualified dimensions, formats, sample counts, compute limits, and memory
profiles. `OPENAGC_ROOT`, `VULKAN_HEADERS_ROOT`, and `OPENAGC_PSBC_ROOT` remain
configurable. An installed SDK consumer resolves both Vulkan-Headers and
OpenAGC through the exported CMake package dependencies.

## Build

```sh
cmake -S . -B build -DVULKAN_HEADERS_ROOT=../Vulkan-Headers
cmake --build build
ctest --test-dir build --output-on-failure
```

Applications link `VulkanPS5::ICD` after installing the package, or link
`libvulkan_ps5.a` directly. They use only standard Vulkan headers and APIs.

The Khronos validation test is enabled automatically when the host Vulkan loader
and Validation Layers are installed. It exercises instance/device lifecycle,
property chains, memory, buffers, images, views, render-pass/framebuffer objects,
command buffers, submission, and fences while failing on any VVL warning/error.

Runtime shader compilation, real PS5 OpenAGC command submission, and VideoOut WSI
remain subsequent milestones. Pipeline creation currently returns
`VK_ERROR_FEATURE_NOT_PRESENT`, and optional shader, sparse, protected, external
handle, multiview, YCbCr, and timeline features remain unadvertised.
