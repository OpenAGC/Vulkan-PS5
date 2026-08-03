# FW 5.50 Eden teardown quarantine (2026-08-03)

## Evidence boundary

Three cleanup-first eight-frame Eden runs completed normally. The following
launcher request timed out and its snapshot kernel log is empty, so the
captured evidence does not identify a kernel fault or prove a specific root
cause.

The audit nevertheless found a concrete unsafe lifetime: OpenAGC registered
swapchain buffers with VideoOut but did not unregister them before Vulkan
freed their image mappings. OpenAGC now performs checked unregister-before-
close teardown in commit `ed02ab8`. This Vulkan change consumes that checked
contract.

## Fail-closed ownership

Swapchain destruction retains the present chain, images, and memory if native
teardown fails. The surface becomes terminal, recreation is rejected, device
destruction is blocked by explicit WSI ownership, and surface destruction
refuses to free a live or quarantined swapchain. This deliberately leaks the
current process rather than unmapping memory that VideoOut or `/dev/gc` may
still own.

Replacement swapchains now wait for idle and unregister the retired native
chain before registering a new main-display chain. Acquire and present reject
terminal surfaces, present rejects retired swapchains, and the swapchain lock
stays held across the native present call. Destruction acquires that same lock
before unregistering the native chain, so unregister cannot overlap a flip.

Native present-fence, compute-queue, graphics-queue, and device destroy results
are checked. Successful phases clear their handles, making a later destroy
call resume safely. The lifecycle test injects a stop after fence destruction
and verifies that the retry completes without double destruction.

## Offline validation

- host lifecycle and WSI executables: PASS;
- guarded swapchain-runner behavior test: PASS;
- Prospero `vulkan_ps5_static` build: PASS;
- OpenAGC generic runtime: 20,085 assertions, zero failures;
- OpenAGC Prospero static build and VideoOut teardown source audit: PASS.

The unrelated pre-existing `tests/pipeline.c` call-signature mismatch prevents
an unscoped all-target build, but the changed host targets and Prospero static
library build cleanly.

## Next hardware gate

No hardware qualification is claimed. After a fresh boot, use only the direct
backend and continuous klog capture. Run one cleanup-first bounded canary and
require checked teardown, exact process absence, a clean scoped kernel log, and
a responsive console before attempting the identical two-run 600-frame gate.
