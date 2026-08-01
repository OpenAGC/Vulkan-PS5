#version 450

layout(push_constant) uniform PushConstants {
    uvec4 value;
} pc;

layout(location = 0) out uvec4 output_color;

void main()
{
    output_color = pc.value;
}
