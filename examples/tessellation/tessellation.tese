#version 450

layout(triangles, equal_spacing, ccw) in;

void main()
{
    const vec4 positions[3] = vec4[](
        vec4(-0.75, -0.75, 0.0, 1.0),
        vec4( 0.75, -0.75, 0.0, 1.0),
        vec4( 0.00,  0.75, 0.0, 1.0)
    );
    vec4 position = gl_TessCoord.x * positions[0] +
                    gl_TessCoord.y * positions[1] +
                    gl_TessCoord.z * positions[2];
    gl_Position = vec4(position.xy * 0.625, 0.0, 1.0);
}
