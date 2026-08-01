# 4x color resolve qualification — 2026-08-01

## Outcome

`vkCmdResolveImage` now records general 4x-to-1x 2D color resolves through a
reproducible graphics-meta fragment shader and public OpenAGC resources,
pipelines, descriptors, transitions, and draws. Unsupported forms fail closed.

The initial hardware probe exposed an OpenAGC image-view bug: ordinary mip-view
patching overwrote gfx1013's multisample `LAST_LEVEL` encoding, so
`sampler2DMS` could read sample zero but not samples one through three. OpenAGC
commits `6d3655f` and `af5c5d5` preserve and qualify the sample-count field.
The final probe uses two in-clip triangles so its source fixture has exact,
unambiguous 32x32 coverage.

## Reproducibility and host gates

- `tools/regenerate_meta_shaders.sh --check`: byte-identical.
- Generic suite: 48/48 passed.
- ASan/UBSan suite: 48/48 passed.
- Prospero static and shared builds: clean.
- Pinned ELF SHA-256:
  `acd7aaf9b536f9335d1d69609eaa5a80d366ad040df6e7ce48fe8f6ddfb211de`.

## Hardware evidence

The exact pinned ELF passed twice on each qualified endpoint. Every run read
back all 1,024 pixels as `0xff00ff80`, the exact average of the four programmed
samples, then self-exited without a residual process and allowed immediate
relaunch.

- FW 11.600.005:
  `20260801T103722Z-resolve-run1.log`,
  `20260801T104007Z-resolve-run1.log`.
- FW 5.500.008:
  `20260801T104036Z-resolve-run1.log`,
  `20260801T104102Z-resolve-run1.log`.

Only the already-qualified raw-ELF `amount=0x4000` VM warning appeared in the
matching kernel logs. Further development slices use FW 5.50 for iteration;
the next FW 11.60 replay is deferred until the final pinned release candidate.
