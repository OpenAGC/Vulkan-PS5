# FW 5.50 variable-pointer qualification

`variablePointers` and `variablePointersStorageBuffer` are qualified for the
Vulkan-PS5 compute path on gfx1013 firmware `0x05500008`.

The bounded SPIR-V 1.3 probe dispatches one 64-invocation workgroup. Every lane
selects between distinct StorageBuffer pointers for a load and a store, then
between distinct Workgroup pointers for a store and load. The host validates
all 1,024 dwords: 64 loads, 64 selected stores, 64 Workgroup round trips, and
zero guards outside the intended ranges. Marker values remain within the
allocation even under a wrong address calculation, preventing the diagnostic
from repeating the original out-of-range GPU fault.

## Root cause

SPIR-V-to-NIR and ACO IR preserved every selected pointer correctly. The
failure was at dispatch: openagc-psbc emits PS5 compute shaders as Wave32, but
Vulkan initialized `AgcGfx1013ComputeState.modifier` to zero. On gfx1013 that
selects the Wave64 dispatch default. Divergent Wave32 code consequently
executed with the wrong lane model, corrupting selected addresses and, with
the original large markers, writing outside the buffer.

OpenAGC now exposes named Wave32 and Wave64 compute dispatch modifiers and
locks the Wave32 initiator to `0x8041` in its host packet fixture. Vulkan sets
`AGC_GFX1013_COMPUTE_DISPATCH_WAVE32` for its Wave32 compiler output. A
shader-side memory barrier was tested, produced the identical corruption
pattern, and was removed; applications need no workaround.

## Evidence

- OpenAGC clean generic build: 4,334 passed, 0 failed.
- Vulkan normal and ASAN/UBSAN suites: 34/34 each.
- Release Prospero link includes `-lunwind -lc++abi -lc++ -lm`.
- Repeated public feature-query/device-request runs:
  - `20260728T144957Z-variable-pointers-run1.log`
  - `20260728T145026Z-variable-pointers-run1.log`
- Exact oracle on both runs:
  `variable_pointers: PASS invocations=64 storage_load=64 storage_store=64 workgroup=64`
- Public Prospero ELF SHA-256:
  `d6d0669f82d2fcd7bac06099eaee6aa9511c8620744548d0a952001779d2702f`

Both runs exited through SystemService, left no stale process, and produced no
PID-scoped GPU fault or reset. The only kernel warning was the established
single raw-ELF `amount=0x4000` baseline.
