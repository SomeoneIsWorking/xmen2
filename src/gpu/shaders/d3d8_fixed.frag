/*
 * The fixed-function pixel stage: two evidenced texture stages and alpha test.
 *
 * D3D8's texture stage state is a small combiner language (D3DTOP_MODULATE,
 * SELECTARG1, ADD, ...) and this implements the subset the engine actually
 * uses for untextured, single-texture, and Dead Zone water drawing. An
 * operation outside that subset must be REFUSED by the code that builds the
 * pipeline, not silently approximated here -- a wrong combiner looks like a
 * lighting bug and gets
 * attributed to anything but the shader.
 */
#version 450

layout(location = 0) in vec4 v_color;
layout(location = 1) in vec2 v_uv;
layout(location = 2) in vec3 v_dir;
layout(location = 3) in vec2 v_uv1;
layout(location = 4) in vec4 v_shadow;
layout(location = 5) in vec3 v_shadow_normal;

layout(location = 0) out vec4 o_color;

/* SDL_GPU binds fragment samplers at set 2 and uniform buffers at set 3. */
layout(set = 2, binding = 0) uniform sampler2D   tex0;
/*
 * The cube sampler is DECLARED ALWAYS and BOUND ALWAYS, even for a draw that
 * uses neither. Vulkan does not allow an unbound sampler a shader declares,
 * and a build without a validation layer does not fail -- it reads undefined
 * texels. The binding side supplies a 1x1 white cube when the draw has no
 * real one; see gpu_draw.cpp.
 */
layout(set = 2, binding = 1) uniform samplerCube texcube;
layout(set = 2, binding = 2) uniform sampler2D tex1;
layout(set = 2, binding = 3) uniform sampler2DShadow shadow_map;

layout(set = 3, binding = 0) uniform PixelState {
    uint  texture_op;      /* 0 none, 1 modulate, 2 select-arg1, 3 add,
                              4 select-arg2 */
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
    uint  stage1_enabled, stage1_color_op;
    uint  stage1_color_arg1, stage1_color_arg2;
    uint  stage1_alpha_op, stage1_alpha_arg1, stage1_alpha_arg2;
    uint  stage1_pad;
    uint  shadow_enabled;
    float shadow_darkness;
    uint  shadow_pad0, shadow_pad1;
    vec4  shadow_depth_plane;   /* world position -> camera view depth */
    vec4  shadow_light;         /* xyz travel direction, w one tile texel in UV */
    vec4  shadow_step;          /* filter tap spacing per cascade, tile UV */
    vec4  shadow_cascade[4];    /* split far, blend start, depth bias, normal offset */
    mat4  shadow_vp[4];         /* world -> each cascade's light clip box */
} fs;

/* One cascade's filtered visibility: a 3x3 grid of hardware-compared bilinear
 * taps inside the cascade's tile of the atlas. 1 = lit, 0 = fully shadowed;
 * `known` is 0 when the point falls outside the tile's light box. */
float cascade_visibility(int c, vec3 world, vec3 normal, float facing,
                         out float known)
{
    vec4 params = fs.shadow_cascade[c];
    vec3 offset_world = world + normal * (params.w * (1.0 - facing));
    vec3 s = (fs.shadow_vp[c] * vec4(offset_world, 1.0)).xyz;
    vec2 uv = vec2(s.x * 0.5 + 0.5, 0.5 - s.y * 0.5);
    known = 0.0;
    if (uv.x <= 0.0 || uv.x >= 1.0 || uv.y <= 0.0 || uv.y >= 1.0
        || s.z <= 0.0 || s.z >= 1.0)
        return 1.0;
    known = 1.0;
    float texel = fs.shadow_light.w;
    float step_uv = fs.shadow_step[c];
    vec2 tile = vec2(float(c & 1), float(c >> 1));
    float lit = 0.0;
    for (int y = -1; y <= 1; y++)
        for (int x = -1; x <= 1; x++) {
            /* Taps stay inside the tile so the bilinear footprint never reads
             * a neighbouring cascade. */
            vec2 tap = clamp(uv + vec2(float(x), float(y)) * step_uv,
                             vec2(texel), vec2(1.0 - texel));
            lit += textureLod(shadow_map,
                              vec3((tile + tap) * 0.5, s.z - params.z), 0.0);
        }
    return lit / 9.0;
}

float shadow_visibility()
{
    if (fs.shadow_enabled == 0u || v_shadow.w <= 0.0) return 1.0;
    vec3 world = v_shadow.xyz;
    float depth = dot(vec4(world, 1.0), fs.shadow_depth_plane);
    int c = 0;
    while (c < 4 && depth >= fs.shadow_cascade[c].x) c++;
    if (c == 4) return 1.0;

    float normal_length = length(v_shadow_normal);
    vec3 normal = normal_length > 1e-4 ? v_shadow_normal / normal_length
                                       : vec3(0.0);
    float facing = clamp(dot(normal, -fs.shadow_light.xyz), 0.0, 1.0);

    float known;
    float visibility = cascade_visibility(c, world, normal, facing, known);
    float band = fs.shadow_cascade[c].x - fs.shadow_cascade[c].y;
    float blend = clamp((depth - fs.shadow_cascade[c].y) / band, 0.0, 1.0);
    if (blend > 0.0) {
        /* The last cascade fades to unshadowed instead of cutting off. */
        float next_visibility = 1.0;
        float next_known = 1.0;
        if (c < 3)
            next_visibility = cascade_visibility(c + 1, world, normal, facing,
                                                 next_known);
        visibility = mix(visibility, next_visibility, blend * next_known);
    }
    return 1.0 - fs.shadow_darkness * (1.0 - visibility);
}

vec4 combiner_arg(uint which, vec4 diffuse, vec4 current, vec4 texel)
{
    if (which == 1u) return texel;
    if (which == 2u) return fs.tfactor;
    if (which == 3u) return current;
    return diffuse;
}

vec4 combine(uint op, vec4 a1, vec4 a2)
{
    if (op == 1u) return a1 * a2;                       /* MODULATE   */
    if (op == 2u) return a1;                            /* SELECTARG1 */
    if (op == 3u) return vec4(a1.rgb + a2.rgb, a1.a);   /* ADD        */
    if (op == 4u) return a2;                            /* SELECTARG2 */
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
        vec4 ca1 = combiner_arg(fs.color_arg1, v_color, c, t);
        vec4 ca2 = combiner_arg(fs.color_arg2, v_color, c, t);
        vec4 aa1 = combiner_arg(fs.alpha_arg1, v_color, c, t);
        vec4 aa2 = combiner_arg(fs.alpha_arg2, v_color, c, t);
        c.rgb = combine(fs.texture_op, ca1, ca2).rgb;
        c.a   = combine(fs.alpha_op, aa1, aa2).a;
    }
    if (fs.stage1_enabled != 0u) {
        vec4 t1 = texture(tex1, v_uv1);
        vec4 ca1 = combiner_arg(fs.stage1_color_arg1, v_color, c, t1);
        vec4 ca2 = combiner_arg(fs.stage1_color_arg2, v_color, c, t1);
        vec4 aa1 = combiner_arg(fs.stage1_alpha_arg1, v_color, c, t1);
        vec4 aa2 = combiner_arg(fs.stage1_alpha_arg2, v_color, c, t1);
        c.rgb = combine(fs.stage1_color_op, ca1, ca2).rgb;
        c.a = combine(fs.stage1_alpha_op, aa1, aa2).a;
    }
    c.rgb *= shadow_visibility();

    /* D3DCMP_GREATEREQUAL is what the engine sets when it enables the alpha
       test; anything else is refused where the state is read. */
    if (fs.alpha_test != 0u && c.a < fs.alpha_ref) discard;

    o_color = c;
}
