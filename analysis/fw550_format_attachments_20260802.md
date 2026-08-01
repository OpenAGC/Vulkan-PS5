# FW 5.50 scalar/vector attachment qualification — 2026-08-02

The cleanup-guarded `vulkan_ps5_format_attachments_probe.elf` renders one
pixel through each of the 36 advertised scalar/vector and packed color
attachment formats. Float/normalized, unsigned, and signed fragment shaders
receive reflected push constants, use attachment-specific SPI export formats,
and transition each image from host write to color target to host read. Every
fence wait is bounded to two seconds.

The discovery run returned safely but found RGB10A2 UINT mismatch
`0x00b458d3` versus `0xb458d123`. Mesa's gfx10.3 format table identified the
cause: logical R10G10B10A2 targets require CB format
`COLOR_2_10_10_10` (`0x09`), while OpenAGC emitted
`COLOR_10_10_10_2` (`0x08`). The corrected public encoder is covered by exact
packet tests.

Final artifact:

- SHA-256: `e4e2f72bc4356cc8b5a08d3a8f6968069d13456e3ee7b273b98991134fbf3bb5`
- Run 1: `20260801T190225Z-format-attachments-run1.log`
- Run 2: `20260801T190248Z-format-attachments-run1.log`
- Oracle: `PASS formats=36 pixels=36 exact-bits`
- Both runs: status 0, exact-PID absence, immediate relaunch, and only the
  accepted raw-ELF `VM resource leak amount:0x4000` baseline warning.

Host evidence is 59/59 normal and 59/59 ASan/UBSan Vulkan tests, including
the zero-direct-call migration audit. OpenAGC passes 19/19 CTest suites and
19,544 core assertions. `openagc-psbc` passes its full host suite and builds
fresh host and Prospero archives. The identical-byte FW 11.60 replay is
deferred to the final endpoint qualification.
