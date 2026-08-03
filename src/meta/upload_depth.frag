#version 450

layout(set = 0, binding = 0, std430) readonly buffer Source {
    uint words[];
} source_data;

layout(push_constant) uniform Parameters {
    uint base_word;
    uint row_stride_words;
    int destination_x;
    int destination_y;
} parameters;

void main() {
    uint x = uint(int(gl_FragCoord.x) - parameters.destination_x);
    uint y = uint(int(gl_FragCoord.y) - parameters.destination_y);
    gl_FragDepth = uintBitsToFloat(source_data.words[
        parameters.base_word + y * parameters.row_stride_words + x]);
}
