#version 450
layout(constant_id = 0) const float gain = 1.0;
layout(location = 0) in vec3 in_color;
layout(location = 0) out vec4 out_color;
void main() {
    out_color = vec4(in_color * gain, 1.0);
}
