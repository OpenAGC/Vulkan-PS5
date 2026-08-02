#version 450

layout(location = 0) in vec4 geometry_color;
layout(location = 0) out vec4 output_color;

void main()
{
    output_color = geometry_color;
}
