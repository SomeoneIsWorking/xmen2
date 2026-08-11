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

layout(location = 0) in vec4 in_pos;
layout(location = 1) in vec4 in_color;      /* UBYTE4_NORM: B,G,R,A in memory */
layout(location = 2) in vec2 in_uv;

layout(location = 0) out vec4 v_color;
layout(location = 1) out vec2 v_uv;

/* SDL_GPU binds vertex uniform buffers at set 1. */
layout(set = 1, binding = 0) uniform VertexState {
    mat4  mvp;
    vec4  viewport;        /* x, y, width, height in pixels */
    uint  pretransformed;  /* 1 for D3DFVF_XYZRHW */
    uint  has_diffuse;     /* 0 when the vertex format has no diffuse colour */
    uint  pad0, pad1;
} vs;

void main()
{
    if (vs.pretransformed != 0u) {
        /*
         * Pixel coordinates to clip space. D3D's origin is the top-left of the
         * viewport and Y grows downward; Vulkan's clip space has Y growing
         * downward too once the viewport is set up the way SDL_GPU sets it, so
         * only the range changes.
         *
         * RHW is deliberately NOT divided through. It carries 1/w for
         * perspective-correct interpolation of a vertex the game already
         * projected, and this stage has no perspective to correct: dividing
         * would move geometry the engine placed exactly.
         */
        vec2 ndc = vec2(
            (in_pos.x - vs.viewport.x) / vs.viewport.z * 2.0 - 1.0,
            (in_pos.y - vs.viewport.y) / vs.viewport.w * 2.0 - 1.0);
        gl_Position = vec4(ndc, in_pos.z, 1.0);
    } else {
        gl_Position = vs.mvp * vec4(in_pos.xyz, 1.0);
    }
    /*
     * D3DCOLOR is 0xAARRGGBB, so the bytes in memory are B,G,R,A and the
     * attribute arrives in that order.
     *
     * WHITE when the vertex format has no diffuse component. That is D3D8's
     * own answer with lighting disabled, and the attribute is aliased onto the
     * position when there is no colour to point it at -- so reading it would
     * multiply every texel by the float bits of the vertex's X coordinate.
     * With lighting ENABLED the right answer is the lit material colour, which
     * this stage does not compute; white leaves the texture as the artist
     * authored it instead of tinting it with nonsense.
     */
    v_color = vs.has_diffuse != 0u ? in_color.zyxw : vec4(1.0);
    v_uv = in_uv;
}
