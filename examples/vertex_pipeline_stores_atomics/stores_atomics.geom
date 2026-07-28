#version 450

layout(triangles) in;
layout(triangle_strip, max_vertices = 3) out;
layout(set = 0, binding = 0, std430) buffer StageProbe {
    uint atomic_markers[4];
    uint store_markers[4];
} probe;

void main()
{
    if (gl_PrimitiveIDIn == 0) {
        atomicExchange(probe.atomic_markers[3], 0xa7040004u);
        probe.store_markers[3] = 0x57040004u;
    }
    for (int i = 0; i < 3; ++i) {
        gl_Position = gl_in[i].gl_Position;
        EmitVertex();
    }
    EndPrimitive();
}
