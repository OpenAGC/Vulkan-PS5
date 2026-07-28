#version 450
#extension GL_EXT_demote_to_helper_invocation : require

layout(location = 0) out vec4 output_color;

void main()
{
    bool demoted_lane = (int(gl_FragCoord.x) & 1) == 0;
    if (demoted_lane)
        demote;

    float marker = helperInvocationEXT() ? 0.0 : 1.0;
    float helper_slope = abs(dFdx(marker));
    bool oracle_region = gl_FragCoord.x < 64.0 && gl_FragCoord.y < 64.0;
    output_color = oracle_region ?
        (helper_slope > 0.5 ?
            vec4(0.0, 1.0, 0.0, 1.0) : vec4(1.0, 0.0, 0.0, 1.0)) :
        vec4(0.0, 0.0, 1.0, 1.0);
}
