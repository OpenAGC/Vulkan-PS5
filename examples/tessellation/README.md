# Standalone Vulkan tessellation readback

This sample uses a three-control-point patch, a tessellation-control shader
that selects level two on every triangle edge, and a tessellation-evaluation
shader that shrinks the evaluated surface to 62.5 percent. For the initial
hardware gate, both tessellation stages use the same constant control-point
positions as OpenAGC's qualified fixture; patch-output reads remain a separate
unadvertised validation item. The mapped RGBA8 oracle therefore expects
coverage distinct from both the ordinary triangle and geometry samples while
requiring the tessellation stages to execute.

On success it prints:

```text
tessellation: PASS <count> green pixels
```

Two independent FW 5.500.008 runs produced exactly 7200 green pixels, completing
the basic standalone qualification gate. The driver must not advertise
`tessellationShader` until the separate patch-output-read path is also fixed and
hardware-qualified.
