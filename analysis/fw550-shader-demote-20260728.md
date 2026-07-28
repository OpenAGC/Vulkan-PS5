# FW 5.50 shader-demote qualification — 2026-07-28

## Contract

The candidate implements `VK_EXT_shader_demote_to_helper_invocation` through
the existing openagc-psbc SPIR-V/NIR/ACO runtime path. The public ICD enumerates
the device extension, reports
`VkPhysicalDeviceShaderDemoteToHelperInvocationFeaturesEXT::shaderDemoteToHelperInvocation`,
and accepts the extension plus feature at device creation.

The fragment oracle demotes every even-X invocation. Demoted invocations
continue through `helperInvocationEXT()` and provide a zero marker to `dFdx`;
surviving odd-X invocations provide one. A correct post-demote helper lane
therefore makes the surviving oracle pixels green, while all demoted output
writes remain suppressed. The exact expected readback is:

- green derivative oracle: 2,048 pixels;
- blue surviving control: 30,720 pixels;
- zero/suppressed demoted writes: 32,768 pixels;
- red failures and unexpected colors: zero.

The derivative oracle occupies a 64-by-64 area wholly within one rasterized
primitive. An earlier fullscreen diagnostic produced 64 red pixels on a single
diagonal from `(129,3)` through `(255,255)`, identifying the clipped oversized
triangle's internal rasterization seam. The GLSL helper set generated while
processing primitives is implementation-dependent, so that seam was excluded
instead of encoding a PS5-specific exception into the pass criteria. The
failed diagnostic logs are preserved as
`20260728T121943Z-shader-demote-run1.log` and
`20260728T122043Z-shader-demote-run1.log`; both applications self-terminated
and the console remained responsive.

## Verification

- openagc-psbc compiled the `OpDemoteToHelperInvocationEXT` fragment shader
  through SPIR-V, NIR, and ACO without a compiler change.
- Normal host tests: 28/28 passed.
- ASAN/UBSAN host tests: 28/28 passed.
- The complete Prospero build passed and linked `-lunwind -lc++abi -lc++ -lm`.
- Internal FW 5.50 gate:
  `20260728T122309Z-shader-demote-run1.log`.
- Public extension/Features2 FW 5.50 gate:
  `20260728T122623Z-shader-demote-run1.log`.
- Both successful gates reported
  `shader_demote: PASS green=2048 blue=30720 demoted=32768 center=00000000`,
  matching SystemService self-exit, exact-PID absence, and only the established
  single `amount=0x4000` baseline VM warning.
- Public ELF SHA-256:
  `f9980eb6bcf1dbf96bc587fae7dd2e43be84dddfa2a8b13ac6ce6ba22e4d7327`.

No OpenAGC or openagc-psbc source change was required. `opengnm-psbc` was not
modified.
