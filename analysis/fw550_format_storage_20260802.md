# FW 5.50 scalar/vector storage-image qualification — 2026-08-02

The refreshed Eden format inventory advertises storage-image support for 30
R, RG, RGBA, and packed RGB10A2 normalized/integer formats. This gate executes
that contract through ordinary Vulkan objects rather than relying on format
queries or the meta-clear path. Three formatless compute shaders cover float,
unsigned, and signed image classes. Every 4x4 target has a distinct immutable
descriptor set, receives a reflected 16-byte push constant, and is checked
bit-for-bit through its queried linear subresource layout.

The first guarded hardware run stopped cleanly at RGBA8 SNORM: the shader
correctly converted -1.0 to -127 (`0x81`), while the existing clear oracle used
the reserved -128 (`0x80`) encoding. Vulkan's normalized fixed-point rule uses
the range `[-(2^(b-1)-1), 2^(b-1)-1]` for float conversion. The general clear
packer now emits -127/-32767 at the negative endpoint, its unit tests were
corrected, and the full 38-format clear/readback gate was requalified.

Verification:

- Clean generic Vulkan build and all 57 CTest suites: PASS.
- Clean ASAN/UBSAN build and all 57 CTest suites: PASS.
- Clean Prospero static/shared library and complete example build: PASS.
- Storage probe ELF SHA-256:
  `8b15a1053c9f7bdfb57f419f10f0b761563009a8d0056bb3c07d8b9e24d379b2`.
- Corrected 38-format clear probe ELF SHA-256:
  `29a2cd389a895e29275a3c527a6668c217dc53cd897748f02625ad3dc34b60d3`.
- Cleanup ELF SHA-256:
  `9fd6b41cf2ea87989c4217234c6f34c96a1ca5dc482355af1258539db77d4d76`.
- Storage run 1:
  `examples/qualification-logs/20260801T170948Z-format-storage-run1.log`.
- Storage run 2:
  `examples/qualification-logs/20260801T171016Z-format-storage-run1.log`.
- Corrected clear run 1:
  `examples/qualification-logs/20260801T171038Z-api52-rgb9e5-formats-run1.log`.
- Corrected clear run 2:
  `examples/qualification-logs/20260801T171100Z-api52-rgb9e5-formats-run1.log`.

Both storage runs reported
`format_storage: PASS formats=30 pixels=480 exact-bits`; both corrected clear
runs reported `integer_formats: PASS formats=38 pixels=2432 exact-bits`.
Every run used cleanup-first deployment, a two-second fence, complete Vulkan
teardown, exact-PID absence, and immediate relaunch. Target-attributed klogs
contain no panic, reset, timeout, watchdog, or GPU fault. The only diagnostic
is the accepted raw-ELF `0x4000` baseline VM warning.

This evidence qualifies scalar/vector storage-image writes on FW 5.50. It
does not yet qualify sampled-image execution, scalar/vector attachment
exports, or the final identical FW 11.60 replay.
