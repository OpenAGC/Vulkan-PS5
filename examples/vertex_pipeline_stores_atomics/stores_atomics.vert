#version 450

layout(set = 0, binding = 0, std430) buffer StageProbe {
    uint atomic_markers[4];
    uint store_markers[4];
} probe;

const vec2 positions[3] = vec2[](
    vec2(-0.75, -0.75),
    vec2( 0.75, -0.75),
    vec2( 0.00,  0.75)
);

void main()
{
    if (gl_VertexIndex == 0) {
        atomicExchange(probe.atomic_markers[0], 0xa7010001u);
        probe.store_markers[0] = 0x57010001u;
    }
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
}
