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
layout(location = 3) in vec3 in_normal;

layout(location = 0) out vec4 v_color;
layout(location = 1) out vec2 v_uv;
/* The DIRECTION a cube map is sampled with. D3D8 generates it rather than
   reading it from the vertex -- see texgen below. */
layout(location = 2) out vec3 v_dir;

/* SDL_GPU binds vertex uniform buffers at set 1. */
layout(set = 1, binding = 0) uniform VertexState {
    mat4  mvp;
    vec4  viewport;        /* x, y, width, height in pixels */
    uint  pretransformed;  /* 1 for D3DFVF_XYZRHW */
    uint  has_diffuse;     /* 0 when the vertex format has no diffuse colour */
    uint  lighting;        /* D3DRS_LIGHTING */
    uint  nlights;         /* how many entries of light[] are enabled */

    mat4  world;           /* lighting is computed in WORLD space */
    vec4  global_ambient;  /* D3DRS_AMBIENT */
    vec4  mat_diffuse;
    vec4  mat_ambient;
    vec4  mat_emissive;
    uint  has_normal;      /* 0 when the vertex format carries no normal */
    uint  color_vertex;    /* D3DRS_COLORVERTEX */
    /*
     * D3DTSS_TEXCOORDINDEX's generator bits: 0 none, 1 camera-space
     * REFLECTION vector, 2 camera-space normal, 3 camera-space position.
     * The engine's environment-mapped characters use 1 with an FVF of
     * position and normal ONLY -- there are no texture coordinates in the
     * vertex to sample a cube with, which is the whole reason this exists.
     */
    uint  texgen;
    uint  programmable;
    mat4  worldview;       /* camera space, where the generators are defined */
    /* Each light is five vec4s: diffuse, ambient, (position, range),
       (direction, type), (attenuation, unused). Packed by hand because a
       std140 array of structs would pad every member to 16 bytes anyway. */
    vec4  light[8 * 5];
} vs;

/*
 * D3D8 fixed-function lighting, the subset this title uses.
 *
 * Per vertex, in world space, because that is where D3D8 puts the lights:
 *
 *   colour = emissive + ambient_material * (global_ambient + sum(light_ambient))
 *          + sum over lights of  diffuse_material * light_diffuse * N.L * atten
 *
 * NOT here, and each is reported by name where the state is read rather than
 * left to look applied: specular, spot cones (a spot lights as a point), and
 * D3DRS_*MATERIALSOURCE selection.
 */
vec4 lit_colour(vec3 wpos, vec3 wnormal, vec4 vertex_diffuse, bool has_n)
{
    vec4 diffuse_material = (vs.color_vertex != 0u && vs.has_diffuse != 0u)
                                ? vertex_diffuse : vs.mat_diffuse;
    vec3 acc = vs.mat_emissive.rgb + vs.mat_ambient.rgb * vs.global_ambient.rgb;
    /*
     * NO NORMAL is a lit case, not an unlit one.
     *
     * D3D8 takes the vertex normal as (0,0,0) when the FVF carries none, which
     * kills every N.L term and leaves emissive + ambient standing. A zero
     * normal here does exactly that, because max(dot(0, L), 0) is 0 -- but it
     * must not go through normalize(), which is undefined for a zero vector and
     * would put a NaN into every channel.
     */
    vec3 n = has_n ? normalize(wnormal) : vec3(0.0);

    for (uint i = 0u; i < vs.nlights; i++) {
        vec4 ldiff = vs.light[i * 5u + 0u];
        vec4 lamb  = vs.light[i * 5u + 1u];
        vec4 lpos  = vs.light[i * 5u + 2u];   /* xyz position, w range */
        vec4 ldir  = vs.light[i * 5u + 3u];   /* xyz direction, w type  */
        vec4 latt  = vs.light[i * 5u + 4u];
        vec3 to_light;
        float atten = 1.0;

        if (ldir.w == 3.0) {                   /* D3DLIGHT_DIRECTIONAL */
            to_light = -normalize(ldir.xyz);
        } else {
            vec3 d = lpos.xyz - wpos;
            float dist = length(d);
            if (dist > lpos.w) continue;       /* outside the light's range */
            to_light = dist > 0.0 ? d / dist : vec3(0.0, 0.0, 1.0);
            float den = latt.x + latt.y * dist + latt.z * dist * dist;
            atten = den > 0.0 ? 1.0 / den : 1.0;
        }
        acc += vs.mat_ambient.rgb * lamb.rgb * atten;
        acc += diffuse_material.rgb * ldiff.rgb *
               max(dot(n, to_light), 0.0) * atten;
    }
    return vec4(min(acc, vec3(1.0)), diffuse_material.a);
}

void main()
{
    if (vs.programmable != 0u) {
        gl_Position = in_pos;
    } else if (vs.pretransformed != 0u) {
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
     * With lighting ENABLED the right answer is the lit material colour, and
     * that is what lit_colour computes -- including for a vertex with no
     * normal, which is where this used to fall through to white.
     *
     * THE SKY WAS WHITE BECAUSE OF THAT FALL-THROUGH. The menu's sky dome is a
     * 16-triangle strip of stride 12: position and nothing else -- no texture,
     * no diffuse, no normal. Gating the whole lighting path on a normal being
     * present sent it to `vec4(1.0)`, and an untextured white draw covering the
     * horizon is a white sky. D3D8 lights it: with no normal every N.L term is
     * zero and the material's emissive and ambient terms are what remain, which
     * is exactly how an engine colours a sky dome.
     */
    vec4 diffuse = vs.has_diffuse != 0u
        ? (vs.programmable != 0u ? in_color : in_color.zyxw) : vec4(1.0);
    if (vs.lighting != 0u && vs.pretransformed == 0u) {
        vec3 wpos = (vs.world * vec4(in_pos.xyz, 1.0)).xyz;
        /* The normal by the world matrix's upper 3x3. Correct for the rigid
           and uniformly-scaled transforms an engine of this age uses; a
           non-uniform scale would need the inverse transpose, and this stage
           does not have one to give. `in_normal` is only READ when the vertex
           format has one -- otherwise the attribute is aliased onto other
           bytes, and transforming those would be reading rubbish however
           little the result contributed. */
        bool has_n = vs.has_normal != 0u;
        vec3 wnrm = has_n ? mat3(vs.world) * in_normal : vec3(0.0);
        v_color = lit_colour(wpos, wnrm, diffuse, has_n);
    } else {
        v_color = diffuse;
    }
    v_uv = in_uv;

    /*
     * Texture-coordinate generation, in CAMERA space.
     *
     * D3D8 defines the reflection vector as R = I - 2(N.I)N with I the
     * normalised vector from the eye to the vertex -- in camera space the eye
     * is the origin, so I is the normalised camera-space position. The normal
     * goes through the same matrix's upper 3x3, with the same caveat about
     * non-uniform scale as the lighting above.
     */
    v_dir = vec3(0.0, 0.0, 1.0);
    if (vs.texgen != 0u) {
        vec3 cpos = (vs.worldview * vec4(in_pos.xyz, 1.0)).xyz;
        vec3 cnrm = normalize(mat3(vs.worldview) * in_normal);
        if (vs.texgen == 1u) {
            vec3 i = normalize(cpos);
            v_dir = i - 2.0 * dot(cnrm, i) * cnrm;
        } else if (vs.texgen == 2u) {
            v_dir = cnrm;
        } else {
            v_dir = cpos;
        }
    }
}
