# FW 5.50 Fragment Stores and Atomics Qualification

## Scope

This qualification closes Eden's mandatory core
`fragmentStoresAndAtomics` gap without adding application-specific behavior.
Legacy and Features2 queries report the feature, and ordinary device creation
accepts it.

The contract applies to otherwise supported storage resources. Vulkan-PS5
does not advertise storage-image format support, while its graphics descriptor
tables support storage buffers. The qualified path therefore covers both
stores and atomic operations on fragment-stage SSBOs without implying
unsupported storage-image capabilities.

## Exact oracle

The fragment shader compiles to an explicit SPIR-V `OpAtomicIAdd` and an SSBO
`OpStore`. Each covered fragment increments one shared counter and writes
marker `0x51a7c0de` to a unique word selected by `gl_FragCoord`. The verifier
requires all three independently observed counts to match exactly:

- 18,432 green framebuffer pixels;
- 18,432 atomic increments; and
- 18,432 marked SSBO words, with untouched guard words remaining zero.

This proves both operation classes and prevents a framebuffer-only pass from
masking missing or duplicated storage side effects.

## Safety and results

The bounded runner uses exact-PID stale-process cleanup, PID-scoped fatal/reset
detection, a five-second device fence wait, and post-run process inspection.
Its fake clean and fatal lifecycle paths pass. Both complete host suites pass
32/32 tests, including ASAN/UBSAN, and the complete Prospero build links
`-lunwind -lc++abi -lc++ -lm`.

- `20260728T131002Z-fragment-stores-atomics-run1.log`: internal feature path
  passed exact `covered=18432 atomic=18432 stores=18432 marker=51a7c0de`.
- `20260728T131203Z-fragment-stores-atomics-run1.log`: final public legacy
  query/request path passed the same exact oracle.

Both runs completed SystemService self-exit and left no matching process. The
target klogs contain only the established single `amount=0x4000` baseline VM
warning, with no PID-scoped fatal signal or GPU reset. The public ELF SHA-256
is `ecbd369db08ae5d7dd80fb66da45d282ef5134ca3d4c614940d1a86e5a2da985`.

The Eden profile now reports
`extensions=0 features=9 limits=0 queues=0 total=9`.
