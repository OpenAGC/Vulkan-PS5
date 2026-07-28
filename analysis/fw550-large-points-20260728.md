# FW 5.50 Large-Points Qualification

`largePoints` was qualified with a bounded point-list readback probe on
FW 5.500.008. The probe requests the feature normally, draws shader-exported
8-, 16-, and 32-pixel points, verifies exact square coverage of 64, 256, and
1,024 pixels, checks the center color, exits through SystemService, verifies
the exact target PID is absent, and confirms websrv remains responsive.

OpenAGC commit `949ca76` provides typed gfx1013 primitive topology and the
atomic point-size/min/max/line-width register packet. Vulkan pipeline creation
records point-list topology and the `[1, 64]` point-size limits with `0.125`
granularity. Host pipeline and command tests lock primitive type 1 and the exact
packet; normal and ASAN/UBSAN suites pass 26/26.

The first internal run (`20260728T115108Z-large-points-run1.log`) stopped before
GPU submission: openagc-psbc's ACO path rejected `store_deref` generated for a
function-local three-element size array. Queue pointers remained idle and the
console stayed responsive. Rewriting the lookup as an equivalent ternary made
the shader compile without modifying the compiler.

The corrected internal run (`20260728T115250Z-large-points-run1.log`) and the
public feature-query/request run (`20260728T115744Z-large-points-run1.log`)
both passed the exact 64/256/1,024 oracle and exited cleanly. The public run used
PID 118, which was absent afterward. Its target-only klog contains no GPU reset
or crash, only the established `amount=0x4000` baseline VM warning. The final
Prospero ELF links `-lunwind -lc++abi -lc++ -lm` and has SHA-256
`439a18445742d30595b1a2e850d5e5370c8e38fc8c55a9beb09b634d3fb9130f`.
