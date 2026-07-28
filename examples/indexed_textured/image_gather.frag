#version 450

layout(set = 0, binding = 0) uniform sampler2D source_texture;
layout(location = 0) out vec4 output_color;

void main()
{
    const ivec2 offsets[4] = ivec2[](
        ivec2( 0, -1),
        ivec2(-1, -1),
        ivec2(-1,  0),
        ivec2( 0,  0)
    );
    output_color = textureGatherOffsets(
        source_texture, vec2(0.5), offsets, 0);
}
