#version 450

layout(push_constant) uniform PushConstants {
    vec4 value;
} pc;

layout(location = 0) out vec4 output_color;

void main()
{
    output_color = pc.value;
}
