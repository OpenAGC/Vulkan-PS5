# Implementation Status

## Milestone 1: host ICD lifecycle

Implemented:

- All 165 Vulkan 1.0/1.1 core entrypoints are exported and present in the ICD
  proc tables. Promoted KHR aliases are available for the advertised instance
  extensions.
- Loader magic and dispatchable-object initialization for instances, physical
  devices, devices, queues, and command buffers.
- Instance/device/queue lifecycle, device groups, Vulkan 1.1 input/output
  `pNext` chains, external-capability rejection, and protected/sparse rejection.
- Host-backed memory, buffers, images, views, synchronization, command pools,
  command buffers, queries, descriptors, render-pass/framebuffer objects, and
  valid pipeline-cache serialization.
- Thread-safe allocation-count enforcement, custom-allocation failure cleanup,
  and concurrent lifecycle coverage.
- Loader and Validation Layer tests. The VVL callback treats both warnings and
  errors as test failures.
- The ICD links OpenAGC transitively and consumes its versioned gfx1013
  capability snapshot instead of duplicating memory, format, sample-count,
  image-dimension, Wave32, MRT, and compute-limit definitions.

Capability evidence:

- OpenAGC's FW 5.50 qualification records establish the exposed R8/RG8/RGBA8,
  BGRA8, RGB10A2, R11G11B10, R/RG/RGBA 16/32-bit float, sRGB, D16, D32, S8,
  D16+S8, and D32+S8 format families.
- OpenAGC bounds image dimensions at 16,384 and color targets at eight.
- OpenAGC's verified PS5 memory profiles use a 4 GB write-back/onion range with
  4 KB alignment and a 12 GB write-combined/garlic range with 2 MB alignment.
- Remaining shader-stage limits use conservative gfx10/RADV bounds and do not
  enable the corresponding optional Vulkan features before shader qualification.

Deliberately unavailable before Milestone 2 or later:

- Graphics and compute pipeline compilation (`VK_ERROR_FEATURE_NOT_PRESENT`).
- Real OpenAGC GPU execution from the host command representation.
- Sparse and protected resources, external handles, multiview, YCbCr conversion,
  timeline semaphores, descriptor indexing, and VideoOut WSI.
