# FW 5.50 scalar/vector sampled-image qualification — 2026-08-02

This gate executes nearest sampling for all 38 uncompressed scalar, vector,
packed, and shared-exponent image formats in the frozen Eden format matrix.
Each 1x1 linear image is initialized through its host-visible queried
subresource layout, transitioned from `PREINITIALIZED`/host-write to
`GENERAL`/shader-read, sampled by the matching float, unsigned, or signed
compute shader, and compared as four exact 32-bit result components.

## Retired synchronization experiment

An earlier version initialized the sampled images with Vulkan's compute-meta
clear path. Repeated runs exposed nondeterministic missing components after a
compute-write to copy-source transition. An attempted runtime workaround
inserted `EVENT_WRITE(CS_PARTIAL_FLUSH, index 4)`, inferred from Mesa rather
than qualified through the public OpenAGC PS5 runtime contract. The artifact
with SHA-256
`6b056b3467dc02900d90e61fd49f633a7b2d6dd1009d2d5ed41c7b86d1f7061d`
caused a user-observed FW 5.50 kernel panic after its guarded launch timed out.
It produced no captured application output and must not be rerun.

The event packet was removed completely. OpenAGC's recovered event builder has
firmware evidence for other event forms, not for this event/index combination;
therefore importing it into a firmware-neutral resource transition was unsafe.
The sampled-format gate now uses host initialization so it qualifies sampling
without depending on the separate compute-meta-clear completion question.

## Corrected implementation and evidence

The corrected OpenAGC descriptor path encodes the queried gfx10.3 linear row
pitch and selects alpha one for RGB565 formats, which store no alpha channel.
It emits no `CS_PARTIAL_FLUSH` event packet.

Verification:

- Focused generic command-recording test: PASS.
- ASAN/UBSAN sampled probe: PASS.
- Prospero sampled probe build with `-Wall -Wextra -Wpedantic`: PASS.
- Sampled probe ELF SHA-256:
  `e4e5dc18fd53933a7d810e5f123a30e5c2249e9c75e24b922f9d8169bc38ad19`.
- Guarded run 1:
  `examples/qualification-logs/20260801T180543Z-format-sampling-run1.log`.
- Immediate identical-ELF replay:
  `examples/qualification-logs/20260801T180610Z-format-sampling-run1.log`.

Both corrected runs reported
`format_sampling: PASS formats=38 components=152 exact-bits`, used a
two-second fence bound per submitted format, completed teardown, and left no
matching target process. Their target-attributed klogs contain no panic,
reset, timeout, watchdog, or GPU fault; only the accepted raw-ELF `0x4000`
baseline VM warning remains.

This evidence qualifies nearest sampled-image execution for the 38-format
matrix on FW 5.500.008. Scalar/vector attachment exports and the final
identical FW 11.60 replay remain separate gates.
