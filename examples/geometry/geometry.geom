#version 450

layout(triangles) in;
layout(triangle_strip, max_vertices = 3) out;
layout(location = 0) out vec4 geometry_color;

void main()
{
    for (int i = 0; i < 3; ++i) {
        gl_Position = vec4(gl_in[i].gl_Position.xy * 0.5, 0.0, 1.0);
        geometry_color = vec4(0.0, 1.0, 0.0, 1.0);
        EmitVertex();
    }
    EndPrimitive();
}
