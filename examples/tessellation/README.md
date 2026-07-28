# Standalone Vulkan tessellation readback

This sample uses a three-control-point patch, a tessellation-control shader
that selects level two on every triangle edge, and a tessellation-evaluation
shader that shrinks the evaluated surface to 62.5 percent. The mapped RGBA8
oracle therefore expects coverage distinct from both the ordinary triangle and
geometry samples while requiring the tessellation stages to execute.

On success it prints:

```text
tessellation: PASS <count> green pixels
```

The FW 5.50 qualification gate requires two independent successful runs before
the driver advertises `tessellationShader` support.
