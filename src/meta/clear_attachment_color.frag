#version 450

layout(push_constant) uniform ClearParameters {
    vec4 color;
} clear_parameters;

layout(location = 0) out vec4 output_color;

void main()
{
    output_color = clear_parameters.color;
}
