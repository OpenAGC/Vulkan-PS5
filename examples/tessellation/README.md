# Standalone Vulkan tessellation readback

This sample uses a three-control-point patch, a tessellation-control shader
that selects level two on every triangle edge, and a tessellation-evaluation
shader that shrinks the evaluated surface to 62.5 percent. The current
qualification candidate copies vertex positions through TCS patch outputs and
reads those positions in TES, exercising the formerly failing offchip path.
The mapped RGBA8 oracle therefore expects
coverage distinct from both the ordinary triangle and geometry samples while
requiring the tessellation stages to execute.

On success it prints:

```text
tessellation: PASS <count> green pixels
```

Two independent FW 5.500.008 runs produced exactly 7200 green pixels, completing
the earlier constant-position qualification gate. Those results do not qualify
this restored patch-output-read candidate. The driver must not advertise
`tessellationShader` until this path is hardware-qualified twice.
