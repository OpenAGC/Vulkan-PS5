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
the earlier constant-position qualification gate. Those results did not by
themselves qualify the restored patch-output-read candidate.

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
retried. The storage-buffer probe then localized the remaining failure to a
missing indirect descriptor-set-table pointer in the separately compiled HS.
After openagc-psbc exposed that ABI and the ICD programmed its compiler-selected
SGPR, two independent bounded runs passed all three hull markers, all copied
positions, and exactly 7200 green pixels
(`20260728T034030Z-tessellation-run1.log` and
`20260728T034211Z-tessellation-run1.log`). Both returned normally and left the
console responsive. This patch-output-read path is now hardware-qualified at
the sample's scope. The ICD consequently advertises `tessellationShader`, and
this sample now queries that bit and enables it during ordinary Vulkan device
creation before constructing the pipeline. The feature-requesting Prospero ELF
has SHA-256
`a1fce3414f4fadac09ff10148d76302bac504ac3befd119e059bd3330877d30d`.
Its one bounded hardware smoke passed all hull markers and copied positions but
produced an all-zero image (`20260728T034904Z-tessellation-run1.log`). It
returned normally, the console remained responsive, and it was not retried.
The result localizes the remaining nondeterminism after TCS execution, in the
offchip-to-TES/raster path; feature exposure is not stable until that path is
corrected and the repeated gate passes again.

The current diagnostic extends the same standard storage buffer to TES. The
unique tessellated vertex with `gl_TessCoord.x` equal to one writes marker
`0x54455300` and copies all three offchip control-point positions. Its oracle
therefore distinguishes a missing TES launch, incorrect offchip reads, and a
downstream raster failure. Both host configurations and the Prospero build
pass. The candidate ELF has SHA-256
`b3e239c757996b7b8296719d461415913e9f1475b601553f28dd5aa06ac65c6e` and
awaits one bounded hardware run after a fresh console-availability signal.
