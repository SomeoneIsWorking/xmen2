/*
 * The fixed-function pixel stage: one texture stage, modulate, alpha test.
 *
 * D3D8's texture stage state is a small combiner language (D3DTOP_MODULATE,
 * SELECTARG1, ADD, ...) and this implements the subset the engine actually
 * uses for untextured and single-texture drawing. An operation outside that
 * subset must be REFUSED by the code that builds the pipeline, not silently
 * approximated here -- a wrong combiner looks like a lighting bug and gets
 * attributed to anything but the shader.
 */
#version 450

layout(location = 0) in vec4 v_color;
layout(location = 1) in vec2 v_uv;

layout(location = 0) out vec4 o_color;

/* SDL_GPU binds fragment samplers at set 2 and uniform buffers at set 3. */
layout(set = 2, binding = 0) uniform sampler2D tex0;

layout(set = 3, binding = 0) uniform PixelState {
    uint  texture_op;      /* 0 none, 1 modulate, 2 select-texture */
    uint  alpha_test;
    float alpha_ref;
    uint  pad0;
} fs;

void main()
{
    vec4 c = v_color;

    if (fs.texture_op == 1u) {
        c *= texture(tex0, v_uv);
    } else if (fs.texture_op == 2u) {
        c = texture(tex0, v_uv);
    }

    /* D3DCMP_GREATEREQUAL is what the engine sets when it enables the alpha
       test; anything else is refused where the state is read. */
    if (fs.alpha_test != 0u && c.a < fs.alpha_ref) discard;

    o_color = c;
}
