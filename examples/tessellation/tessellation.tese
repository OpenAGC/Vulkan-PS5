#version 450

layout(triangles, equal_spacing, cw) in;

void main()
{
    vec4 position = gl_TessCoord.x * gl_in[0].gl_Position +
                    gl_TessCoord.y * gl_in[1].gl_Position +
                    gl_TessCoord.z * gl_in[2].gl_Position;
    gl_Position = vec4(position.xy * 0.625, 0.0, 1.0);
}
