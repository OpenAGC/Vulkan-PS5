#version 450

layout(location = 0, index = 0) out vec4 out_color;
layout(location = 0, index = 1) out vec4 out_blend;

void main()
{
    out_color = vec4(1.0);
    out_blend = vec4(0.0, 1.0, 0.0, 1.0);
}
