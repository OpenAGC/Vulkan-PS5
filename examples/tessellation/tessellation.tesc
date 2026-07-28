#version 450

layout(vertices = 3) out;
layout(set = 0, binding = 0, std430) buffer HullProbe {
    vec4 position[3];
    uint executed[3];
    uint padding;
    uint tes_executed;
    uint tes_padding[3];
    vec4 tes_position[3];
} hull_probe;

void main()
{
    gl_out[gl_InvocationID].gl_Position =
        gl_in[gl_InvocationID].gl_Position;
    hull_probe.position[gl_InvocationID] =
        gl_in[gl_InvocationID].gl_Position;
    hull_probe.executed[gl_InvocationID] =
        0x48530000u + gl_InvocationID;
    if (gl_InvocationID == 0) {
        gl_TessLevelOuter[0] = 2.0;
        gl_TessLevelOuter[1] = 2.0;
        gl_TessLevelOuter[2] = 2.0;
        gl_TessLevelInner[0] = 2.0;
    }
}
