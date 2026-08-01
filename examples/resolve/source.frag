#version 450

layout(location = 0) out vec4 output_color;

void main()
{
    output_color = vec4(float(gl_SampleID) / 3.0, 1.0, 0.0, 1.0);
}
