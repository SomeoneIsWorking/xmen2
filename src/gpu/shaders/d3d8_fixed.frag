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
layout(location = 2) in vec3 v_dir;

layout(location = 0) out vec4 o_color;

/* SDL_GPU binds fragment samplers at set 2 and uniform buffers at set 3. */
layout(set = 2, binding = 0) uniform sampler2D   tex0;
/*
 * The cube sampler is DECLARED ALWAYS and BOUND ALWAYS, even for a draw that
 * uses neither. Vulkan does not allow an unbound sampler a shader declares,
 * and a build without a validation layer does not fail -- it reads undefined
 * texels. The binding side supplies a 1x1 white cube when the draw has no
 * real one; see gpu_draw.c.
 */
layout(set = 2, binding = 1) uniform samplerCube texcube;

layout(set = 3, binding = 0) uniform PixelState {
    uint  texture_op;      /* 0 none, 1 modulate, 2 select-texture, 3 add */
    uint  alpha_test;
    float alpha_ref;
    uint  is_cube;         /* sample texcube with v_dir instead of tex0/v_uv */
} fs;

void main()
{
    vec4 c = v_color;
    vec4 t = fs.is_cube != 0u ? texture(texcube, v_dir) : texture(tex0, v_uv);

    if (fs.texture_op == 1u) {
        c *= t;
    } else if (fs.texture_op == 2u) {
        c = t;
    } else if (fs.texture_op == 3u) {
        /* D3DTOP_ADD: the environment map is ADDED to the lit surface, which
           is what makes a reflection look like a highlight rather than a
           repaint. Alpha comes from the first argument, as D3D8 specifies. */
        c = vec4(c.rgb + t.rgb, c.a);
    }

    /* D3DCMP_GREATEREQUAL is what the engine sets when it enables the alpha
       test; anything else is refused where the state is read. */
    if (fs.alpha_test != 0u && c.a < fs.alpha_ref) discard;

    o_color = c;
}
