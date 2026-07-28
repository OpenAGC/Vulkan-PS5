# FW 5.50 shader-clip-distance qualification — 2026-07-28

## Contract

The candidate implements core `shaderClipDistance` through the existing
openagc-psbc SPIR-V/NIR/ACO runtime path and compiler-derived gfx1013 shader
register state. The public ICD reports the feature through legacy and Features2
queries and accepts it at device creation.

The vertex oracle emits `gl_ClipDistance[0] = position.x` for a triangle that
normally covers 18,432 pixels. Correct user clipping removes the negative-X
half and must produce:

- exactly 9,216 green pixels;
- zero unexpected nonzero pixels;
- a zero sample inside the clipped left half;
- a green sample inside the retained right half;
- zero samples at both top outer corners.

The first hardware run produced the exact 9,216-pixel count and clipped-left
sample, but the diagnostic sampled x=192 outside the triangle at its center
row. That runner failure is preserved as
`20260728T123347Z-shader-clip-distance-run1.log`. Moving the right sample to
x=144 corrected the oracle without changing shader or driver behavior.

## Verification

- openagc-psbc compiled the clip-distance vertex shader through SPIR-V, NIR,
  and ACO without a compiler or OpenAGC source change.
- Normal host tests: 29/29 passed.
- ASAN/UBSAN host tests: 29/29 passed.
- The complete Prospero build passed and linked `-lunwind -lc++abi -lc++ -lm`.
- Internal FW 5.50 gate:
  `20260728T123525Z-shader-clip-distance-run1.log`.
- Public legacy query/request FW 5.50 gate:
  `20260728T123848Z-shader-clip-distance-run1.log`.
- Both successful gates reported
  `shader_clip_distance: PASS green=9216 left=00000000 right=ff00ff00`, matching
  SystemService self-exit, exact-PID absence, and only the established single
  `amount=0x4000` baseline VM warning.
- Public ELF SHA-256:
  `64a4246ff57161364aa84cacb9377fe42b0e886ffce61851ab9329c88c163a31`.

No OpenAGC or openagc-psbc source change was required. `opengnm-psbc` was not
modified.
