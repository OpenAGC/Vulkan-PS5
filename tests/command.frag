#version 450

layout(set = 0, binding = 0) uniform sampler2D source_texture;
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;
layout(location = 1) out vec4 secondary_color;

void main()
{
    color = texture(source_texture, uv);
    secondary_color = vec4(1.0) - color;
}
