# Standalone Vulkan geometry-shader readback

This sample inserts a geometry shader between the ordinary vertex and fragment
stages. The geometry shader shrinks the input triangle by one half in each
dimension, and the host-visible render target is checked for the resulting
roughly one-quarter pixel coverage. This distinct oracle prevents a vertex-only
draw from satisfying the geometry qualification accidentally. It also carries
the green color as a user varying from the geometry shader into the fragment
shader, so missing GS fragment-parameter exports produce a deterministic
readback failure.

On success it prints:

```text
geometry: PASS <count> green pixels
```

The shader path passes FW 5.500.008 with exactly 4,608 green pixels. The ICD
advertises `geometryShader` through both
feature-query forms, accepts legacy and Features2 device requests, and this
sample queries and requests the feature before pipeline creation. All 61 host
tests and the Prospero build pass. The
feature-requesting ELF links with `-lunwind -lc++abi -lc++ -lm` and has SHA-256
`abff21e51e69179ccea2feef874d0920c2229384517a3d0d1ab375a9da89c425`.

Two independent bounded FW 5.500.008 runs produced exactly 4608 green pixels
each (`20260802T033240Z-geometry-run1.log` and
`20260802T033240Z-geometry-run2.log`). Both applications returned normally,
bounded post-run websrv requests confirmed that the console remained
responsive, and the runner did not retry either launch. The standard
`geometryShader` feature-request path is hardware-qualified at this scope.
