# FW 5.50 Vulkan compute/triangle qualification

Qualification completed at `2026-07-27T20:38:50Z` against system software raw
version `0x05500008` (`5.500.008`) using the foreground etaHEN websrv path.

Source revisions:

- Vulkan-PS5: `039fcce`
- OpenAGC: `c893506`
- openagc-psbc: `11728a5`

Prospero artifacts:

- `vulkan_ps5_compute_example.elf` SHA-256
  `60374271f870068cf3d1908e447b3be55dad90d44a1babdde6a00cc0862c068d`
- `vulkan_ps5_triangle_example.elf` SHA-256
  `9a39932f0ab0c7ee3280419e5ea3ba6987c53712e8dc9f40439804e11fa11c2e`

The artifacts link the PS5 runtime libraries `libunwind`, `libc++abi`,
`libc++`, and `libm`. Both applications use ordinary Vulkan 1.1 entrypoints;
firmware credential preparation, `/dev/gc`, submission framing, and the FW
5.50 deferred-descriptor trailer remain inside OpenAGC.

The authoritative command was:

```sh
PS5_HOST=10.0.1.41 examples/run_fw550_m3.sh
```

All required oracles passed:

1. Compute run 1: `compute: PASS 1024 deterministic values`
2. Compute run 2: `compute: PASS 1024 deterministic values`
3. Triangle run 1: `triangle: PASS 18432 green pixels`
4. Triangle run 2: `triangle: PASS 18432 green pixels`

The runner concluded with
`FW550 Milestone 3: PASS (2 compute + 2 triangle runs)`. Complete stdout is
retained locally under `examples/qualification-logs/` and intentionally ignored
by Git. Before qualification, all seven host ICD tests passed, including the
loader, Validation Layers, runtime-pipeline, and command-recording regressions.
