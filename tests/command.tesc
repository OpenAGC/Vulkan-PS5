#version 450

layout(vertices = 3) out;
layout(location = 0) in vec2 in_uv[];
layout(location = 0) out vec2 out_uv[];

void main()
{
    gl_out[gl_InvocationID].gl_Position =
        gl_in[gl_InvocationID].gl_Position;
    out_uv[gl_InvocationID] = in_uv[gl_InvocationID];
    if (gl_InvocationID == 0) {
        gl_TessLevelOuter[0] = 2.0;
        gl_TessLevelOuter[1] = 2.0;
        gl_TessLevelOuter[2] = 2.0;
        gl_TessLevelInner[0] = 2.0;
    }
}
