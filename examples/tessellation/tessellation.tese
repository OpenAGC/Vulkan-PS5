#version 450

layout(triangles, equal_spacing, ccw) in;

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
    if (gl_TessCoord.x > 0.9999) {
        hull_probe.tes_executed = 0x54455300u;
        hull_probe.tes_position[0] = gl_in[0].gl_Position;
        hull_probe.tes_position[1] = gl_in[1].gl_Position;
        hull_probe.tes_position[2] = gl_in[2].gl_Position;
    }
    vec4 position = gl_TessCoord.x * gl_in[0].gl_Position +
                    gl_TessCoord.y * gl_in[1].gl_Position +
                    gl_TessCoord.z * gl_in[2].gl_Position;
    gl_Position = vec4(position.xy * 0.625, 0.0, 1.0);
}
