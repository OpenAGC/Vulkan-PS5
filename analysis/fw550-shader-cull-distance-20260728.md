# FW 5.50 shader-cull-distance qualification — 2026-07-28

## Contract

The candidate implements core `shaderCullDistance` through the existing
openagc-psbc SPIR-V/NIR/ACO runtime path and compiler-derived gfx1013 shader
register state. The public ICD reports the feature through legacy and Features2
queries and accepts it at device creation.

The vertex oracle emits two disjoint, equal-area triangles. All three vertices
of the left primitive receive `gl_CullDistance[0] = -1.0`; all three vertices
of the right control receive `1.0`. Each primitive covers 4,608 pixels, so
correct primitive culling must produce:

- exactly 4,608 green pixels rather than the unculled 9,216;
- zero unexpected nonzero pixels;
- a zero sample inside the culled left primitive;
- a green sample inside the retained right primitive;
- a zero center sample between the primitives.

## Verification

- openagc-psbc compiled the cull-distance vertex shader through SPIR-V, NIR,
  and ACO without a compiler or OpenAGC source change.
- Normal host tests: 30/30 passed.
- ASAN/UBSAN host tests: 30/30 passed.
- The complete Prospero build passed and linked `-lunwind -lc++abi -lc++ -lm`.
- Internal FW 5.50 gate:
  `20260728T124816Z-shader-cull-distance-run1.log`.
- Public legacy query/request FW 5.50 gate:
  `20260728T124948Z-shader-cull-distance-run1.log`.
- Both gates reported
  `shader_cull_distance: PASS green=4608 left=00000000 right=ff00ff00`, matching
  SystemService self-exit, exact-PID absence, and only the established single
  `amount=0x4000` baseline VM warning.
- Public ELF SHA-256:
  `82ffa08623bf7632635c0009f51b439f4ae861d699c5b052cebc9bf1343dcabf`.

No OpenAGC or openagc-psbc source change was required. `opengnm-psbc` was not
modified.
