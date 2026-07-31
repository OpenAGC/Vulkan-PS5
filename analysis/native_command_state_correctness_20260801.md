# Native command-state correctness (2026-08-01)

## Push constants

Vulkan push-constant values are stage-local. The ICD now shadows 256 bytes per
native shader stage rather than one command-wide byte array. Recording updates
only the stages named by `stageFlags`; draw and dispatch preparation replay the
reflected range for its owning stage after the OpenAGC pipeline is bound.

OpenAGC's internal reflected resource arena was changed in the same slice to
reserve a separate aligned push-constant slot per stage. This is necessary for
overlapping vertex and fragment ranges to retain different bytes after replay.
Neither public API exposes the arena offsets or GPU addresses.

The command-recording regression writes `0x11223344` to the vertex stage and
`0xaabbccdd` to the fragment stage at byte offset zero and verifies both
stage-local shadows. OpenAGC independently decodes the emitted shader-register
writes and verifies the same two values.

## Buffer barriers

Vulkan buffer access masks describe synchronization scopes; they are not
resource layouts. Before recording a typed OpenAGC transition, the ICD now
queries `agcGetCommandBufferRangeStateInfo` for the barrier's exact byte range
and uses the returned usage and owner as the native prior state. This avoids
declaring `Undefined` after an earlier partial transition made the ICD's
coarse whole-buffer mirror mixed.

The regression transitions a whole buffer to shader-write, changes its first
16 bytes to shader-read, then records a zero-source barrier for that same
range. The sequence succeeds through the exact OpenAGC state query. A
same-usage transition is deliberately retained because it may still encode a
required visibility dependency.

## Qualification state

The focused command-recording tests pass on the generic backend. These changes
alter the Vulkan ICD and OpenAGC library bytes, so all earlier Prospero hashes
are superseded. FW 5.50 qualification remains pending the guarded EGL/Zink
readback, visible presentation, teardown, and immediate-relaunch sequence.
