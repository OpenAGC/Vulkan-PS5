#version 450

layout(vertices = 3) out;
layout(set = 0, binding = 0, std430) buffer StageProbe {
    uint atomic_markers[4];
    uint store_markers[4];
} probe;

void main()
{
    gl_out[gl_InvocationID].gl_Position =
        gl_in[gl_InvocationID].gl_Position;
    if (gl_InvocationID == 0) {
        atomicExchange(probe.atomic_markers[1], 0xa7020002u);
        probe.store_markers[1] = 0x57020002u;
        gl_TessLevelOuter[0] = 2.0;
        gl_TessLevelOuter[1] = 2.0;
        gl_TessLevelOuter[2] = 2.0;
        gl_TessLevelInner[0] = 2.0;
    }
}
