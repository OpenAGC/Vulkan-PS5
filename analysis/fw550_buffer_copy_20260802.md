# FW 5.50 Vulkan buffer-copy qualification — 2026-08-02

The general `vkCmdCopyBuffer` path records public OpenAGC buffer-copy commands
after validating transfer usage, bindings, alignment, bounds, aliasing,
address ranges, and aggregate command space. The hardware oracle uses two
nonzero-offset regions: 64 bytes from source offset 16 to destination offset
32, and 80 bytes from source offset 128 to destination offset 144.

Artifacts:

- Probe ELF SHA-256:
  `8429fb631a76db85b5f2f54e99952c1eafd96670672758c3eb1ba4790789b8e8`.
- Cleanup ELF SHA-256:
  `9fd6b41cf2ea87989c4217234c6f34c96a1ca5dc482355af1258539db77d4d76`.
- Run 1:
  `examples/qualification-logs/20260801T164405Z-buffer-copy-run1.log`.
- Run 2:
  `examples/qualification-logs/20260801T164425Z-buffer-copy-run1.log`.

Both guarded FW 5.500.008 runs reported
`buffer_copy: PASS bytes=144 regions=2 guards=112`. The identical probe bytes
ran after the required cleanup artifact, completed the bounded fence wait,
destroyed all Vulkan objects, self-exited, left the exact PID absent, and
immediately relaunched. Target-attributed klogs contain no panic, reset,
timeout, or GPU fault. The only diagnostic is the accepted raw-ELF `0x4000`
baseline VM warning.
