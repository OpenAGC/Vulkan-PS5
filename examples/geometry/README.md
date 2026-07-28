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

The FW 5.50 qualification gate requires two independent successful runs before
the driver advertises `geometryShader` support.
