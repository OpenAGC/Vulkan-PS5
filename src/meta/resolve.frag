#version 450

layout(set = 0, binding = 0) uniform sampler2DMS source_image;

layout(push_constant) uniform ResolveParameters {
    ivec2 source_destination_delta;
} resolve;

layout(location = 0) out vec4 output_color;

void main()
{
    ivec2 source_pixel = ivec2(gl_FragCoord.xy) +
        resolve.source_destination_delta;
    output_color = (texelFetch(source_image, source_pixel, 0) +
        texelFetch(source_image, source_pixel, 1) +
        texelFetch(source_image, source_pixel, 2) +
        texelFetch(source_image, source_pixel, 3)) * 0.25;
}
