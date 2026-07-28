# FW 5.50 Extended Image Gather Qualification

## Scope

This qualification closes Eden's mandatory core
`shaderImageGatherExtended` gap without adding an application-specific path.
The ICD reports the feature through legacy and Features2 queries and accepts
it during ordinary device creation.

The compiler path consumes SPIR-V `OpCapability ImageGatherExtended` and
`OpImageGather` with `ConstOffsets`. No OpenAGC or openagc-psbc source change
was required for this closure.

## Exact oracle

The probe samples a nearest-filtered, clamp-to-edge checkerboard with
`textureGatherOffsets`. Its four constant offsets redirect all gathered red
components to the same red texel. Correct execution therefore produces one
and only one covered color: opaque white. The verifier requires exactly
18,432 covered and opaque pixels, a single distinct covered color, center
`0xffffffff`, and zero-valued guard pixels outside the triangle.

## Safety and results

The one-shot runner uses bounded waits, exact-PID stale-process cleanup,
PID-scoped fatal/reset detection, and post-run process inspection. Its fake
clean and fatal lifecycle tests pass. Both complete host configurations pass
31/31 tests, including ASAN/UBSAN, and the Prospero build links the required
`-lunwind -lc++abi -lc++ -lm` runtimes.

- `20260728T125653Z-shader-image-gather-run1.log`: the GPU already produced
  exact `covered=18432 opaque=18432 colors=1 center=ffffffff`; the run was
  rejected because the new result label had been placed in the wrong verifier
  branch. It exited cleanly and was not a hardware failure.
- `20260728T125800Z-shader-image-gather-run1.log`: corrected internal probe
  passed exact `covered=18432 center=ffffffff offsets=4` and exited cleanly.
- `20260728T130045Z-shader-image-gather-run1.log`: final public query/request
  path passed the same exact oracle, completed SystemService self-exit, and
  left no matching process.

The final target klog contains only the established single
`amount=0x4000` baseline VM warning, with no PID-scoped fatal signal or GPU
reset. The public ELF SHA-256 is
`de628e54eb7929f484715b0bec441fe8501ccf3c5560c1f01f3479926a2aa679`.

The Eden profile now reports
`extensions=0 features=10 limits=0 queues=0 total=10`.
