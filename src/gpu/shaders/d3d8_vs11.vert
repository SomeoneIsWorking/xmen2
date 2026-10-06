/*
 * A D3D8 VS 1.1 draw: the guest's program (vs11_program.glsl) in front of the
 * fixed-function stage's tail (d3d8_vertex_stage.glsl).
 *
 * The tail receives what the CPU executor's output buffer carried in its
 * place -- oPos as the position, oD0 as the diffuse colour, oT0.xy as the
 * texture coordinate -- and, where that buffer's absent normal and specular
 * attributes were pointed at the position bytes, the position again. The
 * draw's uniforms say neither is present, so neither is read as one.
 */
#version 450
#extension GL_GOOGLE_include_directive : require

#include "vs11_program.glsl"
#include "d3d8_vertex_stage.glsl"

void main()
{
    vs11_run();
    vec4 position = vs11_reg[VS11_OUT_POS];
    d3d8_vertex_stage(position, vs11_reg[VS11_OUT_D0],
                      vs11_reg[VS11_OUT_T0].xy, position.xyz, position);
}
