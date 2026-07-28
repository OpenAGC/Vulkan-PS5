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

The first bounded run of the restored candidate on FW 5.500.008 returned
normally, left the console responsive, and produced a zeroed target
(`20260728T015236Z-tessellation-run1.log`). This is a safe negative result, not
qualification; the runner did not retry it.

Linking the separately compiled TCS and TES interfaces produced the same safe
zeroed result (`20260728T020700Z-tessellation-run1.log`). A subsequent candidate
also encoded the compiler-reported hull LDS allocation in
`SPI_SHADER_PGM_RSRC2_HS`, but its one bounded run again returned a zeroed target
(`20260728T023553Z-tessellation-run1.log`) while etaHEN websrv remained
responsive. Neither failure was retried, and `tessellationShader` remains
disabled.

The next candidate corrects the remaining gfx10.3 hull-LDS packing detail:
allocation is rounded to 1024 bytes before being encoded in 512-byte register
units, making every legal field value even. Host command tests enforce that
invariant. Its one bounded FW 5.500.008 run returned safely and left etaHEN
websrv responsive, but produced another zeroed target
(`20260728T024632Z-tessellation-run1.log`). It was not retried.
