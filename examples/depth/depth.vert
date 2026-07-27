#version 450

layout(location = 0) out vec3 color;

const vec3 positions[9] = vec3[](
    vec3(-0.90, -0.75, -0.50), vec3( 0.10, -0.75, -0.50),
    vec3(-0.40,  0.75, -0.50),
    vec3(-0.90, -0.75,  0.50), vec3( 0.10, -0.75,  0.50),
    vec3(-0.40,  0.75,  0.50),
    vec3( 0.10, -0.75,  0.50), vec3( 0.90, -0.75,  0.50),
    vec3( 0.50,  0.75,  0.50)
);

void main()
{
    gl_Position = vec4(positions[gl_VertexIndex], 1.0);
    color = gl_VertexIndex < 3 ? vec3(0.0, 1.0, 0.0) :
        vec3(1.0, 0.0, 0.0);
}
