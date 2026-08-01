# Cross-Firmware Attachment-Clear Qualification — 2026-08-01

The dynamic-rendering probe qualifies the application-neutral color
attachment/load-clear path introduced by commit `418731a`. It records
`loadOp=CLEAR`, draws after the meta operation, reads back all 64x64 pixels,
and requires exactly `green=1152 clear=2944 center=ff00ff00`.

The candidate ELF SHA-256 is:

`973caa5748468edfc81b2e3d6860eb741c9da52b606a69d0df1fc1c469f13e0e`

Cleanup-first guarded runs passed twice on FW 5.500.008 and twice on FW
11.600.005. Every run reached the exact pixel oracle, self-terminated, left no
matching process, kept websrv reachable, and emitted only the established raw-
ELF `amount=0x4000` baseline warning. The FW 11.60 uploaded ELF was downloaded
again over FTP and reproduced the pinned SHA-256 exactly.

Local evidence logs:

- FW 5.50: `20260801T091354Z-dynamic-rendering-run1.log`
- FW 5.50 immediate relaunch: `20260801T091405Z-dynamic-rendering-run1.log`
- FW 11.60: `20260801T091924Z-dynamic-rendering-run1.log`
- FW 11.60 immediate relaunch: `20260801T092002Z-dynamic-rendering-run1.log`

This evidence qualifies color attachment and color load-operation clearing on
both endpoints. Depth/stencil attachment-clear pixels were not observed by
this probe and remain explicitly unqualified.
