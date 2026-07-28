# FW 5.50 depth, MRT, and query qualification

Date: 2026-07-27 UTC

Console system software: `0x05500008` (`5.500.008`)

Revisions at the start of the run:

- Vulkan-PS5: `34e3357`
- OpenAGC: `658164d`

The retained `20260727T231245Z` runner logs prove two consecutive passes for:

- compute: 1,024 deterministic values
- triangle: 18,432 green pixels
- indexed-textured: 18,432 opaque pixels and 64+ sampled colors
- depth: 12,288 near-green pixels, 9,830 independent far-red pixels, and
  raw D32 counts `43418/12288/9830`
- MRT: 18,432 green pixels in target 0 and 18,432 magenta pixels in target 1

The first query run initialized OpenAGC and compiled VS/PS, then stopped making
progress during GPU submission. Websrv timed out after 30 seconds and the PS5
became unresponsive. This is a failed query hardware gate; it is not counted as
query qualification.

Audit found that `vkCmdResetQueryPool` used `WRITE_DATA` destination 1, cache
policy 3, and the single-address bit for a 68-dword clear. The corrected packet
uses the hardware-sample-proven ME/MEM_GRBM destination 2, cache policy 0,
address incrementing, and write confirmation (`control = 0x00100100`). To
isolate the next hardware test from command-reset PM4, the standalone query
sample now enables `VK_EXT_host_query_reset` and resets its fresh pool through
`vkResetQueryPoolEXT`. The next run must execute only the query ELF first.
The rebuilt sample prints stages immediately before and after `vkQueueSubmit`,
after fence completion, and after query-result retrieval so another timeout can
be localized without inference.

Retained logs are under `examples/qualification-logs/20260727T231245Z-*.log`
and are ignored by Git.

## Staged recovery run (2026-07-28 UTC)

Query qualification was split into independently deployed targets so the
known-good graphics stream is not coupled to all unqualified query packets:

- `lifecycle`: query-pool creation, host reset, and destruction; no query PM4
- `reset`: corrected command-reset `WRITE_DATA`; no occlusion snapshots
- `full`: begin/draw/end occlusion snapshots and result validation

The full stage is excluded from `run_fw550_m3.sh` and requires the explicit
`VULKAN_PS5_ALLOW_UNQUALIFIED_QUERY=YES` acknowledgement. The lifecycle probe
passed on FW `0x05500008` with `green=18432`; its retained log is
`20260728T002915Z-query-lifecycle.log`. This proves the query allocation and
host-reset lifecycle does not reproduce the hang. It does not qualify either
the corrected reset packet or occlusion begin/end PM4.

The reset probe then passed with `green=18432`; its retained log is
`20260728T003040Z-query-reset.log`. This hardware-qualifies the corrected
command-reset `WRITE_DATA` in isolation. Occlusion begin/end snapshots and
`DB_COUNT_CONTROL` remain the only unqualified query packets and must be
audited before the explicitly gated full probe is attempted.

The remaining sequence was checked against Mesa's GFX10 query implementation:
ZPASS uses `EVENT_WRITE` event 21/index 1, while non-precise counting uses
`DB_COUNT_CONTROL=0x11000102`. The 256-byte snapshot allocation covers the
architectural maximum of 16 render backends at 16 bytes each. The Vulkan render
pass prologue applies register defaults before `vkCmdBeginQuery`, so it does not
overwrite the query's count-control state before the draw. An additional
`idle` probe now emits begin/end and availability without a draw and requires
an available zero count before the live-draw probe is attempted.

The idle probe passed with `samples=0 available=1`; its retained log is
`20260728T003428Z-query-idle.log`. The fence completed and the console remained
responsive. This hardware-qualifies the begin/end ZPASS snapshots,
`DB_COUNT_CONTROL` enable/disable, and EOP availability sequence when no
rasterization occurs. Live counter increments during the qualified triangle
draw are the sole remaining full-query hardware boundary.

The full probe subsequently passed twice. Both runs returned
`samples=18432 available=1`, exactly matching `green=18432`; retained logs are
`20260728T003501Z-query-full.log` and
`20260728T003511Z-query-full.log`. The console remained responsive. This
qualifies live gfx1013 occlusion counting on FW `0x05500008`, and query was
restored to the two-run `run_fw550_m3.sh` regression gate.
