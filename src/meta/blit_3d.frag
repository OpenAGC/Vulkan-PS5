#version 450

layout(set = 0, binding = 0) uniform sampler3D source_image;

layout(push_constant) uniform BlitParameters {
    vec4 source_transform;
    vec2 inverse_source_extent;
    float source_z;
} blit;

layout(location = 0) out vec4 output_color;

void main()
{
    vec2 source_pixel = blit.source_transform.xy +
        gl_FragCoord.xy * blit.source_transform.zw;
    output_color = textureLod(source_image,
        vec3(source_pixel * blit.inverse_source_extent, blit.source_z), 0.0);
}
