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
    uint  texture_op;      /* 0 none, 1 modulate, 2 select-arg1, 3 add */
    uint  alpha_test;
    float alpha_ref;
    uint  is_cube;         /* sample texcube with v_dir instead of tex0/v_uv */
    /*
     * The combiner ARGUMENTS, which this stage used to assume rather than
     * read: D3D8's defaults are ARG1 = D3DTA_TEXTURE and ARG2 = D3DTA_CURRENT,
     * and 12,632 draws a run set ARG2 to D3DTA_TFACTOR instead. Substituting
     * the diffuse colour for the texture factor is not a missing feature, it
     * is a wrong colour on 4% of the picture with nothing to show for it.
     * 0 = diffuse/current, 1 = texture, 2 = texture factor.
     */
    uint  color_arg1, color_arg2;
    uint  alpha_op, alpha_arg1, alpha_arg2;
    vec4  tfactor;         /* D3DRS_TEXTUREFACTOR */
} fs;

vec4 combiner_arg(uint which, vec4 diffuse, vec4 texel)
{
    if (which == 1u) return texel;
    if (which == 2u) return fs.tfactor;
    return diffuse;
}

vec4 combine(uint op, vec4 a1, vec4 a2)
{
    if (op == 1u) return a1 * a2;                       /* MODULATE   */
    if (op == 2u) return a1;                            /* SELECTARG1 */
    if (op == 3u) return vec4(a1.rgb + a2.rgb, a1.a);   /* ADD        */
    return a1;
}

void main()
{
    vec4 c = v_color;
    vec4 t = fs.is_cube != 0u ? texture(texcube, v_dir) : texture(tex0, v_uv);

    if (fs.texture_op != 0u) {
        /*
         * Colour and alpha are SEPARATE pipelines in D3D8, with their own
         * operation and their own arguments -- D3DTOP_ADD on the colour with
         * SELECTARG1 on the alpha is an ordinary combination, and folding them
         * into one vec4 op gets the alpha wrong exactly where blending reads
         * it.
         */
        vec4 ca1 = combiner_arg(fs.color_arg1, v_color, t);
        vec4 ca2 = combiner_arg(fs.color_arg2, v_color, t);
        vec4 aa1 = combiner_arg(fs.alpha_arg1, v_color, t);
        vec4 aa2 = combiner_arg(fs.alpha_arg2, v_color, t);
        c.rgb = combine(fs.texture_op, ca1, ca2).rgb;
        c.a   = combine(fs.alpha_op, aa1, aa2).a;
    }

    /* D3DCMP_GREATEREQUAL is what the engine sets when it enables the alpha
       test; anything else is refused where the state is read. */
    if (fs.alpha_test != 0u && c.a < fs.alpha_ref) discard;

    o_color = c;
}
