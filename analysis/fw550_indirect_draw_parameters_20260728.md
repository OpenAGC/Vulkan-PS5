# FW 5.50 indirect DrawID qualification attempt — 2026-07-28

The one-shot indirect-draw gate used Prospero ELF SHA-256
`1ff9889bbc0dbd194339ce8fb9231ae39c34a40e52750f11f816100793387aa7`.
It added compiler-reported `gl_DrawID` metadata to the existing two-command
`firstVertex = 1`, `firstInstance = 1,2` readback probe and attempted to encode
DrawIndex with a Mesa-style gfx10+ 10-dword multi-indirect packet.

The run failed at `vkQueueSubmit` with `VK_ERROR_DEVICE_LOST`. The PID-scoped
kernel log identified PID 156, reported a fatal user-thread signal, found the
graphics queue active/non-empty, and reset the GPU. The process was already
absent when the exact-PID cleanup check ran, and the runner did not retry. The
console recovered. Evidence is retained in:

- `examples/qualification-logs/20260728T083155Z-indirect-draw-run1.log`
- `examples/qualification-logs/20260728T083155Z-indirect-draw-run1-target.klog`

This disproved the 10-dword cross-platform packet assumption for the tested PS5
path. OpenAGC commit `20fb461` restores the previously hardware-qualified
7-dword PS5 multi packet and rejects its reserved DrawIndex controls. Vulkan
now implements DrawID semantics without that packet extension: when a compiled
shader consumes DrawIndex, a Vulkan multi draw is expanded into single indirect
packets and the compiler-selected DrawIndex SGPR is programmed to `0..N-1`.
Shaders that do not consume DrawIndex retain the native PS5 multi packet.

The corrected Prospero candidate is
`cbd05b90dc471644f7e278236abff26845124e69cb0562d8d7727f650b0e87b8`.
Its bounded run also failed during submission and reset the GPU. The actual
native-game process was PID 157; after the crash ShellUI restarted as PID 158.
The old runner selected the newest generic `/app0/eboot.bin` EXEC record, then
the cleanup helper trusted PID 158 after it had been reused and killed
`SceShellUI`. Evidence is retained in:

- `examples/qualification-logs/20260728T084428Z-indirect-draw-run1.log`
- `examples/qualification-logs/20260728T084428Z-indirect-draw-run1.klog`

Every FW 5.50 runner now selects only EXEC records carrying
`category=native_game`. Cleanup and absence checks require the PID and exact
`eboot.bin` process name to match conjunctively, so PID reuse cannot target a
system process. Host regressions include a later fake `category=shell_ui`
record and a direct PID/name mismatch test.

Public `multiDrawIndirect`, `drawIndirectFirstInstance`, and
`shaderDrawParameters` remain false. The sequential-single-packet candidate
must not be repeated; the submission fault needs a materially narrower probe
before another bounded hardware run.

The next bounded diagnostic is
`vulkan_ps5_indirect_parameters_probe.elf`, SHA-256
`43ceea64dccc1983a27065a63defb47e5b7f1163a00e9e28c65c0e15866aa20a`.
It records exactly one non-indexed indirect draw, consumes BaseVertex and
BaseInstance, and does not consume DrawID or enter the multi-draw expansion.
An exact-green readback therefore isolates the compiler-selected
start-vertex/start-instance packet locations from the rejected DrawID paths.
The host suite passes 20/20 and the Prospero link includes `-lunwind`,
`-lc++abi`, `-lc++`, and `-lm`.

The first deployment attempt at `20260728T085707Z` did not launch the ELF:
the initial websrv readiness check passed, but the port-2121 upload connection
timed out. The captured klog was empty, contained no native-game EXEC record,
and the web endpoint was unavailable afterward. This is infrastructure-only
evidence and neither passes nor fails the indirect-parameter hardware gate. No
retry was made.
