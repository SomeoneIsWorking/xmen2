/*
 * The fixed-function vertex stage D3D8 games expect, as one shader.
 *
 * X-Men Legends II is a 2005 fixed-function title: it hands the device a
 * vertex format (an FVF code), matrices, and render states, and expects the
 * driver to build the pipeline. There is no such thing on Vulkan, so this
 * shader IS that pipeline stage -- driven by uniforms rather than by branches
 * the CPU picks between, because the state combinations are few and a
 * uniform branch costs less than a pipeline permutation.
 *
 * Two position conventions, and they are not interchangeable:
 *   D3DFVF_XYZ     -- model space. Transform by the combined world-view-
 *                     projection matrix the device assembled.
 *   D3DFVF_XYZRHW  -- ALREADY in screen space, with 1/w in the fourth
 *                     component. The engine uses these for anything it places
 *                     by pixel: UI, the splash, full-screen quads. They must
 *                     NOT be transformed, only mapped from pixels to clip
 *                     space.
 */
#version 450
#extension GL_GOOGLE_include_directive : require

layout(location = 0) in vec4 in_pos;
layout(location = 1) in vec4 in_color;      /* UBYTE4_NORM: B,G,R,A in memory */
layout(location = 2) in vec2 in_uv;
layout(location = 3) in vec3 in_normal;
layout(location = 4) in vec4 in_specular;   /* UBYTE4_NORM: B,G,R,A in memory */

#include "d3d8_vertex_stage.glsl"

void main()
{
    d3d8_vertex_stage(in_pos, in_color, in_uv, in_normal, in_specular);
}
