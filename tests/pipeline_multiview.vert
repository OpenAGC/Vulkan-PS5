#version 450
#extension GL_EXT_multiview : require

layout(location = 0) out vec3 out_color;

void main()
{
    float x = gl_VertexIndex == 0 ? -1.0 : 1.0;
    float y = gl_VertexIndex == 2 ? 1.0 : -1.0;
    gl_Position = vec4(x + float(gl_ViewIndex) * 0.01, y, 0.0, 1.0);
    out_color = vec3(1.0, 0.0, float(gl_ViewIndex) / 5.0);
}
