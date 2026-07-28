#version 450

layout(location = 0) out vec4 output_color;

void main()
{
    output_color = gl_FragCoord.x < 128.0
        ? vec4(0.0, 1.0, 0.0, 1.0)
        : vec4(1.0, 0.0, 0.0, 1.0);
}
