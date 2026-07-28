#version 450
#extension GL_ARB_shader_draw_parameters : require

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 ignored_texcoord;
layout(location = 0) out vec2 uv;

void main()
{
    bool valid = gl_BaseVertexARB == 1 &&
                 gl_BaseInstanceARB == 1 &&
                 gl_InstanceIndex == 1;
    gl_Position = vec4(position, 0.0, 1.0);
    uv = valid ? vec2(0.75, 0.25) : vec2(0.25, 0.25);
}
