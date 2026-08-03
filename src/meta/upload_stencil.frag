#version 450
#extension GL_ARB_shader_stencil_export : require

layout(set = 0, binding = 0, std430) readonly buffer Source {
    uint words[];
} source_data;

layout(push_constant) uniform Parameters {
    uint base_byte;
    uint row_stride_bytes;
    int destination_x;
    int destination_y;
} parameters;

void main() {
    uint x = uint(int(gl_FragCoord.x) - parameters.destination_x);
    uint y = uint(int(gl_FragCoord.y) - parameters.destination_y);
    uint byte_index = parameters.base_byte +
        y * parameters.row_stride_bytes + x;
    uint packed = source_data.words[byte_index >> 2u];
    gl_FragStencilRefARB = int((packed >> ((byte_index & 3u) * 8u)) & 0xffu);
}
