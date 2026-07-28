#version 450

const vec2 positions[6] = vec2[](
    vec2(-0.875, -0.375),
    vec2(-0.125, -0.375),
    vec2(-0.500,  0.375),
    vec2( 0.125, -0.375),
    vec2( 0.875, -0.375),
    vec2( 0.500,  0.375)
);

void main()
{
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
    gl_CullDistance[0] = gl_VertexIndex < 3 ? -1.0 : 1.0;
}
