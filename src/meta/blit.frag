#version 450

layout(set = 0, binding = 0) uniform sampler2D source_image;

layout(push_constant) uniform BlitParameters {
    vec4 source_transform;
    vec2 inverse_source_extent;
} blit;

layout(location = 0) out vec4 output_color;

void main()
{
    vec2 source_pixel = blit.source_transform.xy +
        gl_FragCoord.xy * blit.source_transform.zw;
    output_color = textureLod(source_image,
        source_pixel * blit.inverse_source_extent, 0.0);
}
