# Standalone Vulkan compute readback

This application uses only standard Vulkan 1.1 APIs. It embeds a SPIR-V
compute shader, writes 1,024 deterministic words through a storage-buffer
descriptor, waits on a Vulkan fence, invalidates the mapped allocation, and
verifies every result before clean teardown.

Build it with `-DVULKAN_PS5_BUILD_EXAMPLES=ON`. A host build verifies that the
application and shader compile and link. Execution/readback is a FW 5.50 gate:
the generic OpenAGC backend captures command submission but does not emulate GPU
shader execution. A successful PS5 run prints:

```text
compute: PASS 1024 deterministic values
```

The Prospero link requires `ps5-payload-libcxx` and
`ps5-payload-openlibm` from `pacbrew-repo`. They provide `libunwind`,
`libc++abi`, `libc++`, and the `libm` compatibility archive used by the
runtime compiler.
