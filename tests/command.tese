#version 450

layout(triangles, equal_spacing, cw) in;
layout(location = 0) in vec2 in_uv[];
layout(location = 0) out vec2 out_uv;

void main()
{
    gl_Position = gl_TessCoord.x * gl_in[0].gl_Position +
                  gl_TessCoord.y * gl_in[1].gl_Position +
                  gl_TessCoord.z * gl_in[2].gl_Position;
    out_uv = gl_TessCoord.x * in_uv[0] +
             gl_TessCoord.y * in_uv[1] +
             gl_TessCoord.z * in_uv[2];
}
