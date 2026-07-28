# Standalone Vulkan geometry-shader readback

This sample inserts a geometry shader between the ordinary vertex and fragment
stages. The geometry shader shrinks the input triangle by one half in each
dimension, and the host-visible render target is checked for the resulting
roughly one-quarter pixel coverage. This distinct oracle prevents a vertex-only
draw from satisfying the geometry qualification accidentally.

On success it prints:

```text
geometry: PASS <count> green pixels
```

The shader path previously passed two independent FW 5.500.008 runs with
exactly 4608 green pixels. The ICD now advertises `geometryShader` through both
feature-query forms, accepts legacy and Features2 device requests, and this
sample queries and requests the feature before pipeline creation. Both
seven-test host configurations and the Prospero build pass. The
feature-requesting ELF links with `-lunwind -lc++abi -lc++ -lm` and has SHA-256
`386aae854e1aaf504a750aa29904c491e35220d52c718c3bcf048f54de6803a4`.
Two new independent FW 5.50 runs are required before this public feature path
is considered hardware-qualified.
