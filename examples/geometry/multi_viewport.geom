#version 450

layout(triangles) in;
layout(triangle_strip, max_vertices = 6) out;

void main()
{
    for (int viewport = 0; viewport < 2; ++viewport) {
        gl_ViewportIndex = viewport;
        for (int vertex = 0; vertex < 3; ++vertex) {
            gl_Position = gl_in[vertex].gl_Position;
            EmitVertex();
        }
        EndPrimitive();
    }
}
