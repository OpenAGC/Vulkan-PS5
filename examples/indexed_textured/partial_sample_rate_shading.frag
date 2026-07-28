#version 450

layout(set = 0, binding = 1, std430) buffer SampleResults {
    uint sample_counts[4];
    uint total_count;
} results;

layout(location = 0) out vec4 output_color;

void main()
{
    atomicAdd(results.total_count, 1u);
    output_color = vec4(0.0, 1.0, 0.0, 1.0);
}
