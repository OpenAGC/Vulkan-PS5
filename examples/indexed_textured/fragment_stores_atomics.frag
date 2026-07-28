#version 450

layout(set = 0, binding = 1, std430) buffer FragmentResults {
    uint fragment_count;
    uint pixel_writes[];
} results;

layout(location = 0) out vec4 output_color;

void main()
{
    uint x = uint(gl_FragCoord.x);
    uint y = uint(gl_FragCoord.y);
    atomicAdd(results.fragment_count, 1u);
    results.pixel_writes[y * 256u + x] = 0x51a7c0deu;
    output_color = vec4(0.0, 1.0, 0.0, 1.0);
}
