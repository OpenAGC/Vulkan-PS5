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
`977a99cf1aa9e99f286c145c9a6f1cf509945be552f76f88f4e1f50d19148af4`.
It records exactly one non-indexed indirect draw, consumes BaseVertex and
BaseInstance, and does not consume DrawID or enter the multi-draw expansion.
An exact-green readback therefore isolates the compiler-selected
start-vertex/start-instance packet locations from the rejected DrawID paths.
The rebuilt candidate includes the host-qualified `vkCmdCopyBuffer` recording
path but does not execute it. The host suite passes 20/20 and the Prospero link includes `-lunwind`,
`-lc++abi`, `-lc++`, and `-lm`.

The first deployment attempt at `20260728T085707Z` did not produce evidence
that the ELF launched: the initial websrv readiness check passed, but the
port-2121 upload connection timed out. The captured klog was empty, contained
no native-game EXEC record, and the web endpoint was unavailable afterward.
The user subsequently reported that the PS5 had crashed since this attempt.
Therefore the timeout must not be classified as harmless infrastructure-only
evidence, even though no target process or GPU submission was captured. No
retry was made, and further PS5 interaction is suspended pending a safer
recovery/debug path.

After an explicit real-PS5 retest request, the one-draw diagnostic launched as
native-game PID 84 and reproduced `VK_ERROR_DEVICE_LOST` at `vkQueueSubmit`.
The sanitized klog records `SIGSEGV`, an active graphics queue, and a GPU reset:

- `examples/qualification-logs/20260728T090836Z-indirect-parameters-run1.log`
- `examples/qualification-logs/20260728T090836Z-indirect-parameters-run1-sanitized.klog`

The signature is deterministic across the 10-dword, sequential DrawID, and
one-draw no-DrawID cases: RIP `0x4000bb`, a reported unmapped write address
ending in `0x003`, and an active graphics queue followed by reset. The address
moved from `0x1c9c9c003` to `0x1c9ca0003` as the per-run allocation moved by
16 KiB. This proves DrawID and multi expansion are not required for the fault.

The root cause is the `DRAW_INDIRECT` initiator. Vulkan left it zero-initialized,
selecting the indexed/DMA source even for a non-indexed draw. Upstream RADV
emits `V_0287F0_DI_SRC_SEL_AUTO_INDEX` for non-indexed indirect and
`V_0287F0_DI_SRC_SEL_DMA` for indexed indirect; OpenAGC's earlier passing
single-indirect hardware fixture likewise used value two. Vulkan now emits
initiator two for non-indexed and zero for indexed, and exact command tests
lock both packet tails.

The initiator-corrected one-draw candidate launched as native-game PID 85. It
completed the GPU submission without a reset and produced the exact readback
oracle `indirect_parameters: PASS green=5736 firstVertex=1 firstInstance=1
draws=1`. This hardware-qualifies the corrected non-indexed initiator and the
BaseVertex/BaseInstance path at the one-draw scope. After printing PASS, the
sample returned through the raw-ELF exit path and received a separate SIGSEGV
at RIP `0x4000bb`; the graphics queue was already complete. The runner also
encountered a NUL byte in the captured klog, causing its text parser to reject
otherwise useful evidence. Evidence is retained in:

- `examples/qualification-logs/20260728T091416Z-indirect-parameters-run1.log`
- `examples/qualification-logs/20260728T091416Z-indirect-parameters-run1-sanitized.klog`

The rebuilt samples now terminate through the qualified SystemService self-kill
lifecycle after Vulkan cleanup instead of returning from `main`. Every bounded
hardware runner removes NUL bytes from captured kernel logs before PID and
lifecycle parsing; all seven runner regressions inject a real NUL byte. The new
one-draw Prospero candidate SHA-256 is
`f2b139a1629141914462a8058ff1ebe5f21da40fd3c8eddf94d6960b6945feb8`.
All 20 normal host tests and all runner safety tests pass, the full Prospero
build passes, and the required `-lunwind`, `-lc++abi`, `-lc++`, and `-lm` link
set is retained.

The one bounded lifecycle rerun passed on FW 5.500.008 as native-game PID 86:

- `examples/qualification-logs/20260728T092357Z-indirect-parameters-run1.log`
- `examples/qualification-logs/20260728T092357Z-indirect-parameters-run1-target.klog`

It reproduced the exact `green=5736`, `firstVertex=1`, `firstInstance=1`,
`draws=1` oracle and printed `system-exit app=0x4016`. The PID-scoped klog
shows matching requester and target app IDs, `All processes exited`, and idle
graphics queues with equal read/write pointers. It contains no PID-scoped fatal
signal and no GPU reset. The runner accepted only the previously isolated
raw-ELF VM warning `amount=0x4000`, and exact PID/name removal confirmed that
PID 86 was absent. The one-draw BaseVertex/BaseInstance gate is therefore
cleanly hardware-qualified. Public `multiDrawIndirect`,
`drawIndirectFirstInstance`, and `shaderDrawParameters` remain false until
their complete multi-draw and shader semantics are hardware-qualified.
