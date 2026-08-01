#version 450

layout(push_constant) uniform ClearParameters {
    float depth;
} clear_parameters;

void main()
{
    gl_FragDepth = clear_parameters.depth;
}
