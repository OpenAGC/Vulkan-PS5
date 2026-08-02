# FW 5.50 dynamic subgroup-broadcast qualification — 2026-08-02

The cleanup-guarded compute probe validates both mandatory Vulkan 1.2 subgroup
feature gaps that were present in the pinned CTS discovery run:
`shaderSubgroupExtendedTypes` and `subgroupBroadcastDynamicId`.

The shader uses a 32-lane workgroup. The broadcast source ID is read from a
storage buffer at runtime, so the command cannot be reduced to a constant-ID
broadcast. Every lane broadcasts lane 7's value and readback requires the
exact result `0x5a5a5a5d` in all 32 destination lanes.

The implementation path is application-neutral:

- openagc-psbc fixes the requested Wave32 size as an authoritative compiler
  constraint and reflects compute `NumWorkGroups` separately from graphics
  vertex-buffer tables.
- OpenAGC reflection API 18 validates the three-dword system-value range and
  programs direct-dispatch X/Y/Z group counts. Indirect dispatch requiring
  this system value remains fail-closed.
- Vulkan-PS5 advertises the dedicated and Vulkan 1.2 feature forms, compiles
  compute pipelines as Wave32, derives descriptor-buffer native usage from
  reflected access at consumption, and mirrors implicit Vulkan command-buffer
  reset into both native streams.

## Evidence

- Console: PS5 FW 5.500.008 at `10.0.1.41`.
- Probe ELF SHA-256:
  `ed800f84e333434be743ec46d1f21db37c8f8eb05915c17a686f0093cb153d61`.
- Cleanup ELF SHA-256:
  `9fd6b41cf2ea87989c4217234c6f34c96a1ca5dc482355af1258539db77d4d76`.
- Run 1: PASS, exact readback, SystemService exit, PID absent after exit.
- Run 2: PASS with the identical probe bytes, exact readback, SystemService
  exit, PID absent after exit.
- Both PID-scoped kernel-log checks contained only the established raw-ELF
  `BASELINE_VM_WARNING amount=0x4000`; no GPU fault or panic signature.

The console later entered sleep. That event was not a kernel panic and is not
qualification-failure evidence.
