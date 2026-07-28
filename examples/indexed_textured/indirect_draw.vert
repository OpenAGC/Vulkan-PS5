#version 450
#extension GL_ARB_shader_draw_parameters : require

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 ignored_texcoord;
layout(location = 0) out vec2 uv;

void main()
{
    int draw = gl_DrawIDARB;
    int expected_instance = draw + 1;
    bool base_vertex_valid = gl_BaseVertexARB == 1;
    bool base_instance_valid = gl_BaseInstanceARB == expected_instance;
    bool instance_index_valid = gl_InstanceIndex == expected_instance;
    float offset = draw == 0 ? -0.5 : draw == 1 ? 0.5 : 0.0;
    gl_Position = vec4(position + vec2(offset, 0.0), 0.0, 1.0);
    uv = !base_vertex_valid ? vec2(0.25, 0.25) :
         !base_instance_valid ? vec2(0.75, 0.75) :
         !instance_index_valid ? vec2(0.25, 0.75) :
         vec2(0.75, 0.25);
}
