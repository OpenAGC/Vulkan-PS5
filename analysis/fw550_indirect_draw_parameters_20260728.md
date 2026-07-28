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
All 19 host tests pass and the required Prospero libraries are linked. Public
`multiDrawIndirect`, `drawIndirectFirstInstance`, and `shaderDrawParameters`
remain false until this corrected candidate passes one fresh bounded FW 5.50
run.
