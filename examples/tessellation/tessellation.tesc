#version 450

layout(vertices = 3) out;

void main()
{
    const vec4 positions[3] = vec4[](
        vec4(-0.75, -0.75, 0.0, 1.0),
        vec4( 0.75, -0.75, 0.0, 1.0),
        vec4( 0.00,  0.75, 0.0, 1.0)
    );
    gl_out[gl_InvocationID].gl_Position = positions[gl_InvocationID];
    if (gl_InvocationID == 0) {
        gl_TessLevelOuter[0] = 2.0;
        gl_TessLevelOuter[1] = 2.0;
        gl_TessLevelOuter[2] = 2.0;
        gl_TessLevelInner[0] = 2.0;
    }
}
