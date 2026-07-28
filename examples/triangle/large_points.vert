#version 450

layout(location = 0) flat out vec4 output_color;

const vec2 positions[3] = vec2[](
    vec2(-0.50, 0.0),
    vec2( 0.00, 0.0),
    vec2( 0.50, 0.0)
);

const vec4 colors[3] = vec4[](
    vec4(0.0, 1.0, 0.0, 1.0),
    vec4(1.0, 0.0, 0.0, 1.0),
    vec4(0.0, 0.0, 1.0, 1.0)
);

void main()
{
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
    gl_PointSize = gl_VertexIndex == 0 ? 8.0 :
        gl_VertexIndex == 1 ? 16.0 : 32.0;
    output_color = colors[gl_VertexIndex];
}
