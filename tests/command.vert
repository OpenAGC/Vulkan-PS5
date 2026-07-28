#version 450
#extension GL_ARB_shader_draw_parameters : require

layout(location = 0) in vec2 position;
layout(location = 0) out vec2 uv;

void main()
{
    int draw_parameters = gl_BaseVertexARB + gl_BaseInstanceARB + gl_DrawIDARB;
    gl_Position = vec4(position + vec2(float(draw_parameters), 0.0), 0.0, 1.0);
    uv = position * 0.5 + 0.5;
}
