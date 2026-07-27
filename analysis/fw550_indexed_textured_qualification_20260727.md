# FW 5.50 Vulkan indexed-textured qualification

Qualification completed at `2026-07-27T22:25:10Z` against system software raw
version `0x05500008` (`5.500.008`) using the foreground etaHEN websrv path.

Source revisions:

- Vulkan-PS5: the commit containing this report (based on `0751418`)
- OpenAGC: `2cacf3d`
- openagc-psbc: `11728a5`

Prospero artifacts:

- `vulkan_ps5_compute_example.elf` SHA-256
  `60374271f870068cf3d1908e447b3be55dad90d44a1babdde6a00cc0862c068d`
- `vulkan_ps5_triangle_example.elf` SHA-256
  `9a39932f0ab0c7ee3280419e5ea3ba6987c53712e8dc9f40439804e11fa11c2e`
- `vulkan_ps5_indexed_textured_example.elf` SHA-256
  `9c0772de8725d472c598ba73adfe45d060fa2d1b2367c725146f46c47b73ea9c`

All artifacts link `libunwind`, `libc++abi`, `libc++`, and `libm`. The
applications use ordinary Vulkan 1.1 entrypoints; firmware credentials,
`/dev/gc`, gfx1013 packet encoding, and submission remain inside OpenAGC.

The indexed-textured sample uploads a 2x2 RGBA8 image using the row pitch
returned by `vkGetImageSubresourceLayout`. Hardware qualification established
that gfx1013 linear images require a 256-byte row pitch: a tightly packed
second row was read as zero, while the corrected layout produces opaque
bilinear interpolation across the triangle.

The authoritative command was:

```sh
PS5_HOST=10.0.1.41 examples/run_fw550_m3.sh
```

All required oracles passed:

1. Compute runs 1 and 2: `compute: PASS 1024 deterministic values`
2. Triangle runs 1 and 2: `triangle: PASS 18432 green pixels`
3. Indexed-textured runs 1 and 2:
   `indexed_textured: PASS 18432 pixels 64+ colors`

The runner concluded with
`FW550 Milestone 3: PASS (2 compute + 2 triangle + 2 indexed-textured runs)`.
Complete stdout is retained locally under `examples/qualification-logs/` and
intentionally ignored by Git.
