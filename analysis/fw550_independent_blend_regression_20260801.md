# FW 5.50 independent-blend migration regression (2026-08-01)

## Failure

The current-commit FW 5.50 regression campaign stopped safely when the
independent-blend probe rendered attachment zero correctly but left attachment
one untouched:

```
independent_blend: mismatch target0=18432/0/ff00ff00 \
target1=0/0/00000000 expected1=80800080
```

The guarded runner completed process cleanup, found no residual process, and
the console remained responsive. This was a rendering regression, not another
kernel panic.

## Root cause and repair

The native OpenAGC migration translated each attachment's blend enable,
factors, operations, and write mask, but did not publish Vulkan's static
`VkPipelineColorBlendStateCreateInfo::blendConstants`. OpenAGC consequently
used its zero initialization for `CB_BLEND_RED/GREEN/BLUE/ALPHA`. The probe's
second target multiplies magenta by a constant factor of 0.5, so a zero
constant explains the exact all-zero result.

Vulkan-PS5 now treats blend constants as native command state only when an
enabled blend equation actually consumes a constant-color or constant-alpha
factor. A native pipeline bind publishes the pipeline's static constants, or
the current command-buffer values for `VK_DYNAMIC_STATE_BLEND_CONSTANTS`.
Pipelines that do not consume constants retain their prior state requirements;
this is important for internal clear and blit pipelines that bind OpenAGC
objects directly.

The generic command-recording regression uses constant color and alpha factors
and inspects the submitted native DCB for exact
`CB_BLEND_RED=0x3e800000` (0.25). The complete command-recording test, including
dynamic rendering and meta clears, passes.

## Corrected hardware replay

- ELF SHA-256:
  `03f9d4ec0d448a0561902067ec76698f5f7940202864b07cbc24ece757e65570`
- Exact oracle:
  `independent_blend: PASS target0=18432 target1=18432 color1=80800080`
- The application exited through SystemService and ps5debug-NG found no
  remaining process.
- The guarded runner reported only the accepted raw-ELF `0x4000` VM warning.
- websrv remained responsive after the run.
- Evidence logs:
  `examples/qualification-logs/20260801T125430Z-independent-blend-run1.log`
  and
  `examples/qualification-logs/20260801T125430Z-independent-blend-run1-target.klog`.

FW 11.60 replay remains deferred to the final endpoint qualification pass.
