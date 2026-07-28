#version 450

layout(location = 0) flat out vec4 output_color;

const vec2 positions[3] = vec2[](
    vec2(-0.30, -0.30),
    vec2( 0.30, -0.30),
    vec2( 0.00,  0.30)
);

void main()
{
    bool point_draw = gl_InstanceIndex != 0;
    float offset = point_draw ? 0.50 : -0.50;
    gl_Position = vec4(positions[gl_VertexIndex] + vec2(offset, 0.0),
        0.0, 1.0);
    output_color = point_draw ? vec4(1.0, 0.0, 0.0, 1.0) :
        vec4(0.0, 1.0, 0.0, 1.0);
}
