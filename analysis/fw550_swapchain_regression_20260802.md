# FW 5.50 Current-Commit Swapchain Regression — 2026-08-02

## Scope

This replay validates the standard Vulkan WSI contract consumed by Eden's PS5
surface bridge. It is not an Eden executable qualification.

- Vulkan-PS5 revision: `e78b64eaf82f3655cb295e960d8397263520178c`
- Console: standard PS5, system software `5.500.008`
- ELF SHA-256: `049b5ad984083518dfe8d69d13c45db63dc995e82d3ad705401e7b9458a08d5c`
- Cleanup ELF SHA-256: `9fd6b41cf2ea87989c4217234c6f34c96a1ca5dc482355af1258539db77d4d76`
- Workload: three-image FIFO swapchain, 1,800 acquire/submit/present cycles
- Vulkan fence and acquire waits: 2,000,000,000 ns upper bound

The uploaded ELF was downloaded after the run and reproduced the exact local
SHA-256. The runner was then strengthened to require pinned local and remote
hashes before launching future swapchain qualifications.

## Result

The workload printed `swapchain: PASS 1800 frames`. Device-idle, command-pool,
swapchain, device, surface, and instance teardown completed. The PID-scoped
kernel log contained no crash, fatal signal, system execute-only violation, or
unexpected warning. It contained the one accepted raw-ELF baseline warning:

`[KERNEL] WARNING: VM resource leak: set:1, res:0, amount:0x4000`

The launched process was absent after its self-termination lifecycle, and the
websrv endpoint responded immediately afterward.

Evidence:

- `examples/qualification-logs/20260801T192338Z-swapchain-run1.log`
- `examples/qualification-logs/20260801T192338Z-swapchain-run1-target.klog`

## Boundary

This proves that current Vulkan-PS5 WSI behavior did not regress while the Eden
surface bridge was added. Eden still needs a Prospero platform build and its own
ELF before the bridge itself can receive hardware qualification.
