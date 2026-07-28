#version 450

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 ignored_texcoord;
layout(location = 0) out vec2 uv;

void main()
{
    uint instance = uint(gl_InstanceIndex);
    float offset = instance == 1u ? -0.5 : instance == 2u ? 0.5 : 0.0;
    gl_Position = vec4(position + vec2(offset, 0.0), 0.0, 1.0);
    uv = instance == 1u ? vec2(0.75, 0.25) :
         instance == 2u ? vec2(0.25, 0.75) : vec2(0.25, 0.25);
}
