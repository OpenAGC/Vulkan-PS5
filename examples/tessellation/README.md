# Standalone Vulkan tessellation readback

This sample uses a three-control-point patch, a tessellation-control shader
that selects level two on every triangle edge, and a tessellation-evaluation
shader that shrinks the evaluated surface to 62.5 percent. The current
qualification candidate copies vertex positions through TCS patch outputs and
reads those positions in TES, exercising the formerly failing offchip path.
The mapped RGBA8 oracle therefore expects
coverage distinct from both the ordinary triangle and geometry samples while
requiring the tessellation stages to execute.

The current diagnostic also binds a standard storage buffer to TCS. Each of
the three hull invocations writes a unique execution marker and the
corresponding `gl_in` position. After the bounded fence wait, the sample
invalidates this mapping and prints `hull probe PASS` only when all markers and
all twelve position components match. A failed marker localizes HS execution
or binding; correct markers with zero positions localize LS-to-HS LDS; a
passing probe with a black image localizes HS offchip storage or TES/ring
consumption.

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

The corrected separate-hull LDS-load candidate also returned safely but left
the image zeroed (`20260728T030535Z-tessellation-run1.log`). It was not
retried. The storage-buffer probe is a materially distinct candidate built to
localize that remaining downstream failure. Host pipeline creation, command
recording, and the Prospero cross-link pass; it has not been run on PS5 yet.
