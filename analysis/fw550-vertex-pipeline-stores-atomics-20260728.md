# FW 5.50 vertex-pipeline stores and atomics qualification

`vertexPipelineStoresAndAtomics` is qualified for Vulkan-PS5's supported SSBO
storage path without application-specific behavior. The public feature is
reported through legacy and Features2 queries and accepted through both device
creation paths.

The deterministic probe builds one combined VS/TCS/TES/GS pipeline. Every
applicable pre-fragment stage executes an atomic exchange to its own word and
a direct store to a second unique word. The host invalidates the mapped buffer
after GPU completion and requires all eight stage-specific constants. The
framebuffer oracle independently requires exactly 7,200 green pixels and no
unexpected covered values.

The first host recording attempt returned `VK_ERROR_FEATURE_NOT_PRESENT`.
openagc-psbc correctly described a vertex-buffer table for the fused
TES-to-GS primitive executable, but the tessellation recorder only allowed
that table on its hull executable. The recorder now allocates one additional
primitive resource-table slot, accepts the compiler-selected table, and
appends descriptor tables after it instead of overwriting it. No compiler or
OpenAGC changes were required.

Both full 33-test host suites pass, including ASAN/UBSAN, VVL, lifecycle,
command-recording, Eden-profile, and bounded-runner safety coverage. The
complete Prospero build links the probe with `-lunwind -lc++abi -lc++ -lm`.

FW 5.500.008 evidence:

- `20260728T133417Z-vertex-pipeline-stores-atomics-run1.log`: public legacy
  query/request path, exact PASS.
- `20260728T133515Z-vertex-pipeline-stores-atomics-run1.log`: repeated public
  legacy query/request path, exact PASS.

Both runs reported
`green=7200 stages=VS,TCS,TES,GS atomic=4 stores=4`, completed matching
SystemService self-exit, left no stale process, and contained only the already
isolated single raw-ELF `amount=0x4000` baseline warning. The public Prospero
ELF SHA-256 is
`e79e33fe4bc5c8f780e1801456e3ea9bae4a1034148d873b039563ac11dd171a`.
