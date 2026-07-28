#version 450

layout(set = 0, binding = 0) uniform samplerCubeArray cube_images;
layout(location = 0) out vec4 out_color;

void main()
{
    out_color = texture(cube_images, vec4(1.0, 0.0, 0.0, 1.0));
}
