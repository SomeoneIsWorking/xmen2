/*
 * A VS 1.1 draw into the shadow map: the guest's program
 * (vs11_program.glsl), then shadow_depth.vert's transform of its oPos.
 */
#version 450
#extension GL_GOOGLE_include_directive : require

#include "vs11_program.glsl"

layout(location = 0) out vec2 v_uv;

layout(set = 1, binding = 0) uniform ShadowCasterState {
    mat4 shadow_mvp;
} shadow;

void main()
{
    vs11_run();
    gl_Position = shadow.shadow_mvp * vs11_reg[VS11_OUT_POS];
    v_uv = vs11_reg[VS11_OUT_T0].xy;
}
