# FW 5.50 native buffer-copy panic and command-storage isolation

Date: 2026-07-31

## Incident

The first Vulkan candidate that moved `vkCmdCopyBuffer` from the legacy
Vulkan-side submit buffer to `agcCmdCopyBuffer` used ELF SHA-256
`0c1355192de8302bee43172540410a3424480bd379e5aa942d0e523f807e25b5`.
After the required cleanup completed, its only launch stopped responding and
the FW 5.50 console kernel-panicked. The candidate produced no application
stdout before the console went down. It is retired and must not be rerun.

The packet audit ruled out the public copy encoding. Both the previously
passing legacy Vulkan artifact and the native candidate emitted the same two
seven-dword `DMA_DATA` packets followed by the same runtime EOP completion
packet. Buffer GPU addresses also came from the same placed OpenAGC memory.
The safety-relevant submission delta was command storage: the old Vulkan
carrier copied the packets into a dedicated flexible-memory mapping, while
the native command buffer was suballocated from the same runtime heap block
that also contained mutable host-visible DMA resources.

## Fix

OpenAGC now gives every kernel-submitted native command buffer a dedicated
flexible-memory mapping. Buffers, images, and other ordinary allocations
continue to use the heap suballocator. Host coverage queries the copy command
buffer's allocation and requires its `dedicated` property to be one.

The Vulkan buffer-copy probe now disables stdio buffering, prints start and
exit stages, and routes every early failure through a wrapper that still calls
SystemService exit. This does not change the copy workload; it makes future
failures observable and prevents an ordinary API error from leaving the
websrv title resident.

## Verification

The fixed Prospero ELF SHA-256 is
`35315b83d6731844d825b932e16dad904003b3a0cc6b4114c261b35455ec4d56`.
It passed the exact 256-byte, two-region workload twice through the
cleanup-required runner on standard PS5 FW `5.500.008`:

- copied 144 bytes across the two disjoint ranges;
- preserved all 112 guard bytes;
- completed the bounded native fence wait;
- printed `stage=exit status=0` and invoked SystemService exit;
- left no matching `eboot.bin` process;
- left ports 8080, 2121, 744, and 3232 reachable;
- produced no panic, GPU fault, GPU reset, watchdog, fatal, or timeout entry in
  either captured target kernel log.

Evidence:

- `examples/qualification-logs/20260731T133910Z-buffer-copy-run1.log`
- `examples/qualification-logs/20260731T133910Z-buffer-copy-run1-target.klog`
- `examples/qualification-logs/20260731T134014Z-buffer-copy-run1.log`
- `examples/qualification-logs/20260731T134014Z-buffer-copy-run1-target.klog`

Both successful exits retained only the established raw-ELF VM warning of
`amount=0x4000`.
