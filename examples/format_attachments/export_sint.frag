#version 450

layout(push_constant) uniform PushConstants {
    ivec4 value;
} pc;

layout(location = 0) out ivec4 output_color;

void main()
{
    output_color = pc.value;
}
