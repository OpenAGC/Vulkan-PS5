#version 450

layout(triangles) in;
layout(triangle_strip, max_vertices = 3) out;
layout(location = 0) in vec2 in_uv[];
layout(location = 0) out vec2 out_uv;

void main()
{
    for (int i = 0; i < 3; ++i) {
        gl_Position = gl_in[i].gl_Position;
        out_uv = in_uv[i];
        EmitVertex();
    }
    EndPrimitive();
}
