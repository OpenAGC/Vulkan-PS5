#version 450

layout(triangles, equal_spacing, ccw) in;
layout(set = 0, binding = 0, std430) buffer StageProbe {
    uint atomic_markers[4];
    uint store_markers[4];
} probe;

void main()
{
    if (gl_TessCoord.x == 1.0 && gl_TessCoord.y == 0.0 &&
        gl_TessCoord.z == 0.0) {
        atomicExchange(probe.atomic_markers[2], 0xa7030003u);
        probe.store_markers[2] = 0x57030003u;
    }
    vec4 position = gl_TessCoord.x * gl_in[0].gl_Position +
                    gl_TessCoord.y * gl_in[1].gl_Position +
                    gl_TessCoord.z * gl_in[2].gl_Position;
    gl_Position = vec4(position.xy * 0.625, 0.0, 1.0);
}
