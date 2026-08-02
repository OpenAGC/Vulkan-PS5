# FW 5.50 Zink idle/teardown regression — 2026-08-02

## Result

The post-render teardown regression is fixed and FW 5.50-qualified. The
corrected Vulkan-PS5 Prospero ICD SHA-256 is
`99308715821fa358980149681cadde4833f6832f8b3c5ff3559939baf5b810f9`.
Two immediate guarded SDL/EGL/Zink launches both produced:

```
INFO: ps5-zink: PASS renderer=zink Vulkan 1.2(PlayStation 5 gfx1013 (host ICD) (MESA_RADV)) rgba=64,128,191,255
```

The runner retired exact PIDs 88 and 89, found no PID-scoped fatal signal,
GFX reset, system power event, or stale process, and verified WebSrv remained
available after each run. Each klog contained only the already-established
raw-homebrew `amount=0x4000` VM warning in the target process scope.

## Cause and fix

The failing run had completed graphics execution and printed its exact pixel
PASS before the console event. OpenAGC's `agcPresent` already waits the supplied
render fence and the VideoOut flip event, so an unobserved asynchronous flip
was not the gap. Vulkan-PS5 instead implemented both `vkQueueWaitIdle` and
`vkDeviceWaitIdle` as unconditional success. That violated the Vulkan
lifecycle contract and allowed concurrent submit/present work to overlap
swapchain or device destruction.

`vkQueueWaitIdle` now acquires the same lock used by native submit and present,
then waits the current OpenAGC presentation-ready fence with a two-second
upper bound. `vkDeviceWaitIdle` delegates to that queue. Swapchain and device
destruction refuse to release native objects if the bounded idle operation
fails, favoring a process-lifetime leak over unsafe teardown.

## Verification

- Fresh generic configure/build completed; 61/61 CTest tests passed.
- The complete fresh Prospero library and example build completed without a
  compiler warning.
- The ICD relocation/export preflight passed before both launches: 204 exports,
  four dependencies, and only supported x86-64 relocation classes.
- Run 1: `20260802T035855Z`, exact pixel PASS, PID 88 retired.
- Immediate run 2: `20260802T035920Z`, exact pixel PASS, PID 89 retired.
- Application-log SHA-256 values were
  `9bd6da31939eafced489d9759527e65aa777cc695d6ad04fd3d93888cb89e05c`
  and
  `2b861984b01a6ca67d254101f070bde327def387816083e4935ff46439fca07f`.

The identical-byte FW 11.60 replay remains deferred to the final endpoint
regression gate.
