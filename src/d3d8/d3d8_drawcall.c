/*
 * Turning the device's state into a draw.
 *
 * This is the one place D3D8's state machine becomes a description the GPU
 * layer can execute, and it is deliberately separate from d3d8_device.c: the
 * device's job is to RECORD what the engine set, and this file's job is to
 * read that record. Mixing them is how a setter starts making rendering
 * decisions.
 *
 * The rule throughout: a state this cannot express is REFUSED, by name, once.
 * The engine sets around ninety render states and this understands a dozen of
 * them; the rest are ignored, and the ignored list is printed at exit so that
 * anything drawn is read next to what was missing from it.
 */
#include "d3d8_drawcall.h"
#include "d3d8_resource.h"
#include "d3d8_state.h"
#include "d3d8_types.h"

#include "gpu_draw.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* D3DRS_*, the ones this reads. */
#define D3DRS_ZENABLE            7
#define D3DRS_ZWRITEENABLE      14
#define D3DRS_ALPHATESTENABLE   15
#define D3DRS_SRCBLEND          19
#define D3DRS_DESTBLEND         20
#define D3DRS_CULLMODE          22
#define D3DRS_ZFUNC             23
#define D3DRS_ALPHAREF          24
#define D3DRS_ALPHAFUNC         25
#define D3DRS_ALPHABLENDENABLE  27
#define D3DRS_LIGHTING         137
#define D3DRS_AMBIENT          139
#define D3DRS_COLORVERTEX      141
#define D3DRS_TEXTUREFACTOR     60

/* D3DTSS_*, the ones this reads. */
#define D3DTSS_COLOROP           1
#define D3DTSS_ADDRESSU          13
#define D3DTSS_ADDRESSV          14
#define D3DTSS_MAGFILTER         16
#define D3DTSS_MINFILTER         17
#define D3DTSS_COLORARG1          2
#define D3DTSS_COLORARG2          3
#define D3DTSS_ALPHAOP            4
#define D3DTSS_ALPHAARG1          5
#define D3DTSS_ALPHAARG2          6
#define D3DTSS_TEXCOORDINDEX     11
#define D3DTSS_TEXTURETRANSFORMFLAGS 24

/* D3DTOP_* */
#define D3DTOP_DISABLE           1
#define D3DTOP_SELECTARG1        2
#define D3DTOP_MODULATE          4
#define D3DTOP_ADD               7

/* D3DPT_* */
#define D3DPT_POINTLIST          1
#define D3DPT_LINELIST           2
#define D3DPT_LINESTRIP          3
#define D3DPT_TRIANGLELIST       4
#define D3DPT_TRIANGLESTRIP      5
#define D3DPT_TRIANGLEFAN        6

/* D3DFVF_* */
#define D3DFVF_XYZ            0x0002
#define D3DFVF_XYZRHW         0x0004
#define D3DFVF_NORMAL         0x0010
#define D3DFVF_PSIZE          0x0020
#define D3DFVF_DIFFUSE        0x0040
#define D3DFVF_SPECULAR       0x0080
#define D3DFVF_TEXCOUNT_MASK  0x0f00
#define D3DFVF_TEXCOUNT_SHIFT 8

/* D3DTS_* */
#define D3DTS_VIEW        2
#define D3DTS_PROJECTION  3
#define D3DTS_WORLD     256

static unsigned long g_ignored[D3D8_MAX_RENDER_STATES];
static unsigned long g_refused_prim, g_refused_fvf;

/*
 * What the texture stage actually resolved to, per draw.
 *
 * A picture with a white sky and a correct floor is a question this answers
 * and guessing does not: an UNTEXTURED draw and a textured one that sampled
 * the wrong thing look identical from the outside, and the difference is
 * whether the engine bound a texture at all.
 */
/*
 * The shader does not read the combiner ARGUMENTS -- it assumes D3D8's
 * defaults, ARG1 = D3DTA_TEXTURE and ARG2 = D3DTA_CURRENT. That assumption has
 * never been checked against the engine, and it is the kind that fails
 * silently: a stage set to modulate the DIFFUSE by the TEXTURE FACTOR instead
 * would come out as an ordinary textured surface of the wrong colour. Counted
 * per draw so the assumption is a measurement.
 */
/*
 * A D3DTA_* value as the shader's argument selector.
 *
 * D3DTA_DIFFUSE and D3DTA_CURRENT are the same thing on a single-stage
 * pipeline -- CURRENT is "the result so far", and at stage 0 that IS the
 * diffuse. Anything else (SPECULAR, TEMP, or a COMPLEMENT/ALPHAREPLICATE
 * modifier) is named once and treated as the diffuse, which is the existing
 * behaviour made explicit rather than a new approximation.
 */
static int ta_of(uint32_t v, const char *what)
{
    switch (v) {
    case 0u: case 1u: return GPU_TA_DIFFUSE;   /* CURRENT is the diffuse here */
    case 2u: return GPU_TA_TEXTURE;
    case 3u: return GPU_TA_TFACTOR;
    default: {
        static int told;
        if (!told++)
            fprintf(stderr, "d3d8: %s = 0x%x is an argument this shader does "
                            "not have (SPECULAR, TEMP, or a modifier); it is "
                            "read as the diffuse colour. Reported once, and it "
                            "is a KNOWN WRONG colour.\n", what, v);
        return GPU_TA_DIFFUSE;
    }
    }
}

static unsigned long g_arg_default, g_arg_other;
static uint32_t g_arg_first[4];
static int g_arg_seen;
static unsigned long g_multistage_draws;
static int g_multistage_max;
static unsigned long g_texop_none_notex, g_texop_none_disabled,
                     g_texop_select, g_texop_modulate, g_texop_add,
                     g_texop_other;

/* ---- the vertex format ------------------------------------------------- */

/*
 * Decode an FVF into offsets.
 *
 * The order is fixed by D3D8 and is not negotiable: position, then normal,
 * then point size, then diffuse, then specular, then texture coordinates.
 * Getting the order wrong reads colour as a coordinate, which draws geometry
 * in the right place with impossible colours -- a symptom that looks like a
 * shading bug.
 */
int d3d8_fvf_layout(uint32_t fvf, D3D8VertexLayout *out)
{
    uint32_t off = 0;
    uint32_t ntex = (fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;

    memset(out, 0, sizeof *out);
    out->color_offset = -1;
    out->uv_offset = -1;
    out->normal_offset = -1;

    if (fvf & D3DFVF_XYZRHW) {
        out->pos_offset = (int)off;
        out->pretransformed = 1;
        off += 16;
    } else if (fvf & D3DFVF_XYZ) {
        out->pos_offset = (int)off;
        off += 12;
    } else {
        /* No position at all: either a vertex shader declaration this host
           does not read, or an FVF the engine built wrongly. Both must stop
           here rather than draw from offset zero. */
        return 0;
    }
    if (fvf & D3DFVF_NORMAL)   { out->normal_offset = (int)off; off += 12; }
    if (fvf & D3DFVF_PSIZE)    off += 4;
    if (fvf & D3DFVF_DIFFUSE)  { out->color_offset = (int)off; off += 4; }
    if (fvf & D3DFVF_SPECULAR) off += 4;
    if (ntex) {
        out->uv_offset = (int)off;
        off += 8u * ntex;          /* two floats each, which is the default */
    }
    out->stride = off;
    return 1;
}


/* ---- state translation ------------------------------------------------- */

static uint32_t rs(const D3D8State *s, uint32_t which, uint32_t dflt)
{
    return s->render[which].set ? s->render[which].value : dflt;
}

/* ---- fixed-function lighting ------------------------------------------- */

/*
 * D3DRS_LIGHTING and everything it reads.
 *
 * D3D8 computes this per VERTEX, in world space: the engine's light positions
 * and directions are world-space, so the shader transforms the vertex and its
 * normal by the WORLD matrix alone and does the arithmetic there. The combined
 * mvp cannot be taken apart again, which is why the world matrix travels
 * separately.
 *
 * D3DMATERIAL8 is 17 floats -- diffuse, ambient, specular, emissive, power --
 * and D3DLIGHT8 is 26 dwords, laid out as type, diffuse, specular, ambient,
 * position, direction, range, falloff, attenuation 0/1/2, theta, phi. Both are
 * copied out by OFFSET here, in one place, rather than being read at three
 * call sites that could each get the layout wrong.
 *
 * NOT implemented, and each is named where it is dropped rather than left to
 * look applied: specular (the engine sets SPECULARENABLE=0), spot cones
 * (falloff/theta/phi -- a spot is treated as a point light and SAYS so), and
 * D3DRS_COLORVERTEX's per-source selection (DIFFUSEMATERIALSOURCE and
 * friends), which uses D3D's defaults.
 */
static void copy4(float *dst, const float *src) { memcpy(dst, src, 4 * sizeof *dst); }

static void argb_to_rgba(uint32_t c, float *out)
{
    out[0] = (float)((c >> 16) & 0xFF) / 255.0f;
    out[1] = (float)((c >>  8) & 0xFF) / 255.0f;
    out[2] = (float)((c      ) & 0xFF) / 255.0f;
    out[3] = (float)((c >> 24) & 0xFF) / 255.0f;
}

static void fill_lighting(const D3D8State *s, GpuDraw *out)
{
    static const float ident[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    static int told_spot, told_toomany;
    const float *w = s->transform_set[D3DTS_WORLD]
                         ? s->transform[D3DTS_WORLD].m : ident;
    unsigned i;

    memcpy(out->world, w, sizeof out->world);
    out->lighting = rs(s, D3DRS_LIGHTING, 0) != 0;
    out->color_vertex = rs(s, D3DRS_COLORVERTEX, 1) != 0;
    argb_to_rgba(rs(s, D3DRS_AMBIENT, 0), out->global_ambient);
    if (!out->lighting) return;

    if (s->material_set) {
        copy4(out->mat_diffuse,  &s->material[0]);
        copy4(out->mat_ambient,  &s->material[4]);
        copy4(out->mat_emissive, &s->material[12]);
    } else {
        /* D3D8's own default material is white diffuse and nothing else. */
        out->mat_diffuse[0] = out->mat_diffuse[1] = out->mat_diffuse[2] =
            out->mat_diffuse[3] = 1.0f;
    }

    out->nlights = 0;
    for (i = 0; i < D3D8_MAX_LIGHTS; i++) {
        const float *L = s->light[i];
        GpuLight *g;
        if (!s->light_set[i] || !s->light_on[i]) continue;
        if (out->nlights == GPU_MAX_LIGHTS) {
            if (!told_toomany++)
                fprintf(stderr, "d3d8: more than %d lights are enabled at "
                                "once; the rest are DROPPED and the scene is "
                                "darker than the engine asked for.\n",
                        GPU_MAX_LIGHTS);
            break;
        }
        g = &out->light[out->nlights++];
        memset(g, 0, sizeof *g);
        g->type = (int)((const uint32_t *)L)[0];
        copy4(g->diffuse, &L[1]);
        copy4(g->ambient, &L[9]);
        memcpy(g->position, &L[13], 3 * sizeof(float));
        memcpy(g->direction, &L[16], 3 * sizeof(float));
        g->range = L[19];
        g->atten[0] = L[21];
        g->atten[1] = L[22];
        g->atten[2] = L[23];
        if (g->type == 2 && !told_spot++)
            fprintf(stderr, "d3d8: a SPOT light is enabled; this stage has no "
                            "cone, so it is lit as a point light -- brighter "
                            "outside the cone than the engine asked for.\n");
    }
}


static GpuBlend blend_of(uint32_t d3d)
{
    switch (d3d) {
    case 1:  return GPU_BLEND_ZERO;
    case 2:  return GPU_BLEND_ONE;
    case 3:  return GPU_BLEND_SRCCOLOR;
    case 4:  return GPU_BLEND_INVSRCCOLOR;
    case 5:  return GPU_BLEND_SRCALPHA;
    case 6:  return GPU_BLEND_INVSRCALPHA;
    case 7:  return GPU_BLEND_DESTALPHA;
    case 8:  return GPU_BLEND_INVDESTALPHA;
    case 9:  return GPU_BLEND_DESTCOLOR;
    case 10: return GPU_BLEND_INVDESTCOLOR;
    default: return (GpuBlend)0;              /* refused by the caller */
    }
}

static GpuCompare cmp_of(uint32_t d3d)
{
    if (d3d >= 1 && d3d <= 8) return (GpuCompare)d3d;   /* the same order */
    return (GpuCompare)0;
}

static GpuPrimitive prim_of(uint32_t d3d, uint32_t count, uint32_t *out_count)
{
    *out_count = count;
    switch (d3d) {
    case D3DPT_TRIANGLELIST:  return GPU_PRIM_TRIANGLELIST;
    case D3DPT_TRIANGLESTRIP: return GPU_PRIM_TRIANGLESTRIP;
    case D3DPT_LINELIST:      return GPU_PRIM_LINELIST;
    default:                  return (GpuPrimitive)0;
    }
}

/*
 * TRIANGLEFAN, expanded into a triangle list.
 *
 * Vulkan -- and so SDL_GPU -- has no fan primitive, and 10,688 draws in one
 * gameplay run were being refused for it: the UI panels, and whatever else the
 * engine fans. The expansion is exact rather than approximate. A fan of n+2
 * vertices is n triangles (v0, vi, vi+1), which is a fact about the primitive,
 * not a reinterpretation of it.
 *
 * The indices generated are ABSOLUTE vertex numbers -- first_vertex and
 * base_vertex are folded in here and zeroed in the draw -- because carrying
 * two different bases through an expansion is how an off-by-one becomes a
 * mesh that is subtly wrong instead of absent.
 *
 * An INDEXED fan is expanded too: a D3D8 buffer's contents live in guest
 * memory and are uploaded from there on Unlock, so the index data is readable
 * on the CPU at draw time. Nothing here reads back from the GPU.
 */
static GpuBuffer g_fan_ib;
static uint32_t  g_fan_ib_bytes;
static unsigned long g_fans_expanded, g_fan_tris;

static int fan_expand(const D3D8DrawRequest *req, GpuDraw *out)
{
    uint32_t tris = req->primitive_count, need, i;
    uint32_t *idx;
    int ok;

    if (!tris) {
        fprintf(stderr, "d3d8: a TRIANGLEFAN with 0 primitives.\n");
        return 0;
    }
    if (req->index_buffer && !req->index_guest_bytes) {
        fprintf(stderr, "d3d8: an indexed TRIANGLEFAN whose index buffer has "
                        "no guest storage; the fan cannot be expanded and the "
                        "draw is refused.\n");
        return 0;
    }
    need = (tris * 3u) * (uint32_t)sizeof(uint32_t);
    if (need > g_fan_ib_bytes) {
        /* Grown, never shrunk: one allocation settles after the largest fan
           the run contains. */
        if (g_fan_ib) gpu_buffer_destroy(g_fan_ib);
        g_fan_ib_bytes = need < 4096u ? 4096u : need;
        g_fan_ib = gpu_buffer_create(GPU_BUF_INDEX, g_fan_ib_bytes);
        if (!g_fan_ib) {
            g_fan_ib_bytes = 0;
            fprintf(stderr, "d3d8: no index buffer for a %u triangle fan.\n",
                    tris);
            return 0;
        }
    }
    idx = (uint32_t *)malloc(need);
    if (!idx) {
        fprintf(stderr, "d3d8: out of memory expanding a %u triangle fan.\n",
                tris);
        return 0;
    }
    if (req->index_buffer) {
        uint32_t base = req->base_vertex;
        if (req->index_is_32bit) {
            const uint32_t *src = (const uint32_t *)(uintptr_t)
                                  req->index_guest_bytes + req->first_index;
            for (i = 0; i < tris; i++) {
                idx[i * 3 + 0] = base + src[0];
                idx[i * 3 + 1] = base + src[i + 1];
                idx[i * 3 + 2] = base + src[i + 2];
            }
        } else {
            const uint16_t *src = (const uint16_t *)(uintptr_t)
                                  req->index_guest_bytes + req->first_index;
            for (i = 0; i < tris; i++) {
                idx[i * 3 + 0] = base + src[0];
                idx[i * 3 + 1] = base + src[i + 1];
                idx[i * 3 + 2] = base + src[i + 2];
            }
        }
    } else {
        uint32_t v0 = req->first_vertex;
        for (i = 0; i < tris; i++) {
            idx[i * 3 + 0] = v0;
            idx[i * 3 + 1] = v0 + i + 1;
            idx[i * 3 + 2] = v0 + i + 2;
        }
    }
    ok = gpu_buffer_upload(g_fan_ib, 0, idx, need);
    free(idx);
    if (!ok) return 0;

    out->prim = GPU_PRIM_TRIANGLELIST;
    out->prim_count = tris;
    out->indices = g_fan_ib;
    out->index_is_32bit = 1;
    out->first_index = 0;
    out->base_vertex = 0;
    out->first_vertex = 0;
    g_fans_expanded++;
    g_fan_tris += tris;
    return 1;
}

/*
 * Build the draw.
 *
 * Returns 0 and says why if the state cannot be expressed. Nothing here
 * substitutes a "close enough" value: a refused draw is a visible hole in the
 * picture that leads straight to this function, and an approximated one is a
 * subtly wrong picture that leads nowhere.
 */
int d3d8_build_draw(const D3D8State *s, const D3D8DrawRequest *req,
                    GpuDraw *out)
{
    D3D8VertexLayout vl;
    uint32_t fvf = s->vertex_shader;
    uint32_t cull, srcb, dstb;

    memset(out, 0, sizeof *out);

    if (!req->vertex_buffer) {
        fprintf(stderr, "d3d8: a draw with no stream source bound.\n");
        return 0;
    }
    /*
     * D3D8 overloads SetVertexShader: a value below 0x10000 with the FVF bits
     * set is a fixed-function format, anything else is a shader handle. A real
     * shader handle cannot be honoured here and must not be silently drawn as
     * if it were fixed-function.
     */
    if (fvf & 0xFFFF0000u) {
        static int told;
        if (!told++)
            fprintf(stderr, "d3d8: the engine bound a real VERTEX SHADER "
                            "(handle 0x%08x). This host implements the "
                            "fixed-function pipeline only, so the draw is "
                            "refused rather than drawn with the wrong "
                            "transform. Reported once.\n", fvf);
        g_refused_fvf++;
        return 0;
    }
    if (!d3d8_fvf_layout(fvf, &vl)) {
        fprintf(stderr, "d3d8: FVF 0x%08x has no position.\n", fvf);
        g_refused_fvf++;
        return 0;
    }
    out->prim = prim_of(req->primitive_type, req->primitive_count,
                        &out->prim_count);
    /* A fan is expanded below, once the buffers and offsets it needs are in
       place -- doing it here would be overwritten by them. */
    if (!out->prim && req->primitive_type != D3DPT_TRIANGLEFAN) {
        static int told;
        if (!told++)
            fprintf(stderr, "d3d8: primitive type %u (a strip of lines, or "
                            "points) is not implemented; the draw is refused. "
                            "Reported once.\n", req->primitive_type);
        g_refused_prim++;
        return 0;
    }

    out->vertices = req->vertex_buffer;
    out->vertex_stride = req->stride ? req->stride : vl.stride;
    out->first_vertex = req->first_vertex;
    out->indices = req->index_buffer;
    out->index_is_32bit = req->index_is_32bit;
    out->first_index = req->first_index;
    out->base_vertex = req->base_vertex;

    if (req->primitive_type == D3DPT_TRIANGLEFAN && !fan_expand(req, out)) {
        g_refused_prim++;
        return 0;
    }

    out->pos_offset = vl.pos_offset;
    out->pretransformed = vl.pretransformed;
    out->color_offset = vl.color_offset;
    out->uv_offset = vl.uv_offset;
    out->normal_offset = vl.normal_offset;

    /*
     * The transform.
     *
     * D3D8 keeps world, view and projection separately and multiplies them in
     * that order; the shader wants one matrix. The multiply happens here
     * because it is the engine's convention being honoured, not a rendering
     * decision.
     */
    d3d8_combine_transform(s, out->mvp);
    fill_lighting(s, out);

    /*
     * How many stages the draw actually ASKED for.
     *
     * "Stage 0 only" has been this backend's limit from the start, and the
     * report has never said what that costs: a draw with a second stage
     * enabled is drawn with the second stage MISSING, which for a lightmap or
     * a detail texture is a picture that is merely wrong rather than absent.
     * Counted here so the size of the gap is a number rather than a worry --
     * and counted per draw, with the total draws as its denominator.
     */
    {
        unsigned st;
        int extra = 0;
        for (st = 1; st < D3D8_MAX_STAGES; st++) {
            uint32_t o = s->stage[st][D3DTSS_COLOROP].set
                             ? s->stage[st][D3DTSS_COLOROP].value : D3DTOP_DISABLE;
            if (o != D3DTOP_DISABLE) extra++;
        }
        if (extra) {
            g_multistage_draws++;
            if (extra > g_multistage_max) g_multistage_max = extra;
        }
    }

    /* Texture stage 0 only. A second stage is a combiner this shader does not
       have, and it is reported rather than dropped. */
    {
        uint32_t op = s->stage[0][D3DTSS_COLOROP].set
                          ? s->stage[0][D3DTSS_COLOROP].value : D3DTOP_MODULATE;
        if (!req->texture || op == D3DTOP_DISABLE) {
            out->texop = GPU_TEXOP_NONE;
            if (!req->texture) g_texop_none_notex++;
            else g_texop_none_disabled++;
        } else if (op == D3DTOP_SELECTARG1) {
            out->texop = GPU_TEXOP_SELECT_TEXTURE;
            g_texop_select++;
        } else if (op == D3DTOP_MODULATE) {
            out->texop = GPU_TEXOP_MODULATE;
            g_texop_modulate++;
        } else if (op == D3DTOP_ADD) {
            /* The environment-map combine: the reflection is ADDED to the lit
               surface. Treating it as MODULATE darkened the character instead
               of highlighting it. */
            out->texop = GPU_TEXOP_ADD;
            g_texop_add++;
        } else {
            g_texop_other++;
            static int told;
            if (!told++)
                fprintf(stderr, "d3d8: texture stage operation %u is not one "
                                "this shader implements; the stage is treated "
                                "as MODULATE. Reported once, and it is a "
                                "KNOWN WRONG colour, not a refusal.\n", op);
            out->texop = GPU_TEXOP_MODULATE;
        }
        /*
         * D3DTSS_TEXCOORDINDEX's top 16 bits are the generator. Read here and
         * translated, so a generator this backend does not implement is a
         * named refusal downstream rather than a silent fall back to the
         * vertex's own coordinates -- which for the FVF these draws use
         * (position and normal, no UVs at all) would be reading the position's
         * bytes as a coordinate.
         */
        {
            uint32_t tci = s->stage[0][D3DTSS_TEXCOORDINDEX].set
                ? s->stage[0][D3DTSS_TEXCOORDINDEX].value : 0;
            switch (tci & 0xFFFF0000u) {
            case 0x00010000u: out->texgen = GPU_TEXGEN_CAMERA_NORMAL;     break;
            case 0x00020000u: out->texgen = GPU_TEXGEN_CAMERA_POSITION;   break;
            case 0x00030000u: out->texgen = GPU_TEXGEN_CAMERA_REFLECTION; break;
            default:          out->texgen = GPU_TEXGEN_NONE;              break;
            }
            if (out->texgen != GPU_TEXGEN_NONE)
                d3d8_worldview_transform(s, out->worldview);
        }
        {
            /* D3DTA_DIFFUSE 0, D3DTA_CURRENT 1, D3DTA_TEXTURE 2,
               D3DTA_TFACTOR 3. The masks above 0xF are modifiers
               (COMPLEMENT/ALPHAREPLICATE) and are still not read -- an
               argument carrying one is counted as `other` and named. */
            uint32_t a1 = s->stage[0][D3DTSS_COLORARG1].set
                ? s->stage[0][D3DTSS_COLORARG1].value : 2u;
            uint32_t a2 = s->stage[0][D3DTSS_COLORARG2].set
                ? s->stage[0][D3DTSS_COLORARG2].value : 1u;
            uint32_t b1 = s->stage[0][D3DTSS_ALPHAARG1].set
                ? s->stage[0][D3DTSS_ALPHAARG1].value : 2u;
            uint32_t b2 = s->stage[0][D3DTSS_ALPHAARG2].set
                ? s->stage[0][D3DTSS_ALPHAARG2].value : 1u;
            if (a1 == 2u && (a2 == 1u || a2 == 0u)
                && b1 == 2u && (b2 == 1u || b2 == 0u)) {
                g_arg_default++;
            } else {
                g_arg_other++;
                if (!g_arg_seen++) {
                    g_arg_first[0] = a1; g_arg_first[1] = a2;
                    g_arg_first[2] = b1; g_arg_first[3] = b2;
                }
            }
            out->color_arg1 = ta_of(a1, "COLORARG1");
            out->color_arg2 = ta_of(a2, "COLORARG2");
            out->alpha_arg1 = ta_of(b1, "ALPHAARG1");
            out->alpha_arg2 = ta_of(b2, "ALPHAARG2");
            {
                uint32_t ao = s->stage[0][D3DTSS_ALPHAOP].set
                    ? s->stage[0][D3DTSS_ALPHAOP].value : D3DTOP_MODULATE;
                out->alpha_op = ao == D3DTOP_SELECTARG1 ? GPU_TEXOP_SELECT_TEXTURE
                              : ao == D3DTOP_ADD        ? GPU_TEXOP_ADD
                              : ao == D3DTOP_DISABLE    ? GPU_TEXOP_NONE
                                                        : GPU_TEXOP_MODULATE;
            }
            argb_to_rgba(rs(s, D3DRS_TEXTUREFACTOR, 0xFFFFFFFFu),
                         out->texture_factor);
        }
        out->texture = req->texture;
        /*
         * A cube bound to the stage is refused downstream, and "cube sampling
         * is not implemented" is only half the question. A cube map is
         * addressed by a DIRECTION, and where that direction comes from is
         * D3DTSS_TEXCOORDINDEX's top bits -- D3DTSS_TCI_CAMERASPACENORMAL
         * (0x10000), _CAMERASPACEPOSITION (0x20000) or
         * _CAMERASPACEREFLECTIONVECTOR (0x30000) -- because an FVF with two
         * texture floats cannot carry one. Implementing the sampler without
         * the generator would draw the reflection from whatever the UVs
         * happened to hold, which is the "plausible-looking wrong reflection"
         * the refusal exists to avoid. So the state is READ and printed rather
         * than assumed, once, with everything needed to decide the work.
         */
        if (req->texture && gpu_texture_is_cube(req->texture)) {
            static int told;
            if (!told++) {
                uint32_t tci = s->stage[0][D3DTSS_TEXCOORDINDEX].set
                    ? s->stage[0][D3DTSS_TEXCOORDINDEX].value : 0;
                uint32_t ttf = s->stage[0][D3DTSS_TEXTURETRANSFORMFLAGS].set
                    ? s->stage[0][D3DTSS_TEXTURETRANSFORMFLAGS].value : 0;
                fprintf(stderr,
                    "d3d8: the first CUBE-textured draw: FVF 0x%08x, "
                    "COLOROP %u, TEXCOORDINDEX 0x%08x (generator %s), "
                    "TEXTURETRANSFORMFLAGS 0x%x.\n"
                    "  A cube is addressed by a direction; that field says "
                    "where this one comes from. Reported once.\n",
                    fvf, op, tci,
                    (tci & 0xFFFF0000u) == 0x00010000u ? "camera-space NORMAL"
                  : (tci & 0xFFFF0000u) == 0x00020000u ? "camera-space POSITION"
                  : (tci & 0xFFFF0000u) == 0x00030000u ? "camera-space "
                                                         "REFLECTION VECTOR"
                  : "none -- the vertex's own texture coordinates",
                    ttf);
            }
        }
        /* D3DTADDRESS_CLAMP is 3; D3DTEXF_POINT is 1. */
        out->texture_clamp = s->stage[0][D3DTSS_ADDRESSU].value == 3;
        out->texture_point = s->stage[0][D3DTSS_MAGFILTER].value == 1;
    }

    out->blend_enable = rs(s, D3DRS_ALPHABLENDENABLE, 0) != 0;
    srcb = rs(s, D3DRS_SRCBLEND, 2);          /* D3DBLEND_ONE */
    dstb = rs(s, D3DRS_DESTBLEND, 1);         /* D3DBLEND_ZERO */
    out->src_blend = blend_of(srcb);
    out->dst_blend = blend_of(dstb);
    if (out->blend_enable && (!out->src_blend || !out->dst_blend)) {
        fprintf(stderr, "d3d8: blend factors %u/%u are not ones this backend "
                        "has; refusing the draw.\n", srcb, dstb);
        return 0;
    }

    out->depth_test = rs(s, D3DRS_ZENABLE, 1) != 0;
    out->depth_write = rs(s, D3DRS_ZWRITEENABLE, 1) != 0;
    out->depth_func = cmp_of(rs(s, D3DRS_ZFUNC, 4));   /* LESSEQUAL */
    if (!out->depth_func) out->depth_func = GPU_CMP_LESSEQUAL;

    cull = rs(s, D3DRS_CULLMODE, 3);          /* D3DCULL_CCW */
    out->cull = cull == 1 ? GPU_CULL_NONE
              : cull == 2 ? GPU_CULL_CW
                          : GPU_CULL_CCW;

    out->alpha_test = rs(s, D3DRS_ALPHATESTENABLE, 0) != 0;
    out->alpha_ref = (float)(rs(s, D3DRS_ALPHAREF, 0) & 0xFFu) / 255.0f;
    return 1;
}

/*
 * world * view * projection, in D3D's order and D3D's row-major storage.
 *
 * The shader multiplies as `mvp * position` with a column-major mat4, which is
 * the same arithmetic as D3D's row-vector `position * M` when the matrix is
 * handed over untransposed -- so no transpose happens here, and that is a
 * deliberate non-action rather than an omission.
 */
/* World * View on its own. D3D8's texture-coordinate generators are all
   defined in CAMERA space, so the shader needs this as well as the combined
   matrix -- and the combined one cannot be taken apart again. */
void d3d8_worldview_transform(const D3D8State *s, float out[16])
{
    static const float ident[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    const float *w = s->transform_set[D3DTS_WORLD]
                         ? s->transform[D3DTS_WORLD].m : ident;
    const float *v = s->transform_set[D3DTS_VIEW]
                         ? s->transform[D3DTS_VIEW].m : ident;
    int i, j, k;
    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++) {
            float acc = 0.0f;
            for (k = 0; k < 4; k++) acc += w[i * 4 + k] * v[k * 4 + j];
            out[i * 4 + j] = acc;
        }
}

void d3d8_combine_transform(const D3D8State *s, float out[16])
{
    static const float ident[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    const float *w = s->transform_set[D3DTS_WORLD]
                         ? s->transform[D3DTS_WORLD].m : ident;
    const float *v = s->transform_set[D3DTS_VIEW]
                         ? s->transform[D3DTS_VIEW].m : ident;
    const float *p = s->transform_set[D3DTS_PROJECTION]
                         ? s->transform[D3DTS_PROJECTION].m : ident;
    float wv[16];
    int i, j, k;

    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++) {
            float acc = 0.0f;
            for (k = 0; k < 4; k++) acc += w[i * 4 + k] * v[k * 4 + j];
            wv[i * 4 + j] = acc;
        }
    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++) {
            float acc = 0.0f;
            for (k = 0; k < 4; k++) acc += wv[i * 4 + k] * p[k * 4 + j];
            out[i * 4 + j] = acc;
        }
}

/* For the heartbeat. The shutdown report cannot answer this: every run of
   this game ends in a kill, so a number that only appears at exit is a number
   nobody ever reads -- the same lesson the pulse and preemption counters
   already cost. */
void d3d8_drawcall_multistage(unsigned long *draws, int *most)
{
    *draws = g_multistage_draws;
    *most = g_multistage_max;
}

void d3d8_drawcall_combiner_args(unsigned long *dflt, unsigned long *other,
                                 uint32_t first[4])
{
    int i;
    *dflt = g_arg_default;
    *other = g_arg_other;
    for (i = 0; i < 4; i++) first[i] = g_arg_first[i];
}

/*
 * The render states this file actually READS.
 *
 * It exists so the report cannot drift from the code. The list of
 * "set and not implemented" used to be a second, independent table in
 * d3d8_state.c, and it went stale exactly as one would expect: lighting was
 * implemented and the report went on announcing D3DRS_LIGHTING, D3DRS_AMBIENT
 * and D3DRS_COLORVERTEX as "missing from the picture" for every run after.
 * A reader who believed it would have gone looking for lighting that was
 * already there.
 *
 * Now the answer comes from the one file that does the reading, so
 * implementing a state removes it from the report by construction.
 */
int d3d8_drawcall_reads_state(uint32_t which)
{
    static const uint32_t READ[] = {
        D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_ALPHATESTENABLE,
        D3DRS_SRCBLEND, D3DRS_DESTBLEND, D3DRS_CULLMODE, D3DRS_ZFUNC,
        D3DRS_ALPHAREF, D3DRS_ALPHAFUNC, D3DRS_ALPHABLENDENABLE,
        D3DRS_LIGHTING, D3DRS_AMBIENT, D3DRS_COLORVERTEX, D3DRS_TEXTUREFACTOR
    };
    unsigned i;
    for (i = 0; i < sizeof READ / sizeof READ[0]; i++)
        if (READ[i] == which) return 1;
    return 0;
}

void d3d8_drawcall_note_ignored_state(uint32_t which)
{
    if (which < D3D8_MAX_RENDER_STATES) g_ignored[which]++;
}

void d3d8_drawcall_report(void)
{
    if (g_refused_prim)
        printf("        %lu draw(s) refused for an unimplemented primitive "
               "type\n", g_refused_prim);
    if (g_fans_expanded)
        printf("        %lu TRIANGLEFAN(s) expanded into %lu triangle(s) -- "
               "Vulkan has no fan primitive, so each becomes an index list\n",
               g_fans_expanded, g_fan_tris);
    if (g_refused_fvf)
        printf("        %lu draw(s) refused for a vertex format this host "
               "cannot express (a real vertex shader, or no position)\n",
               g_refused_fvf);
    printf("        %lu draw(s) enabled a texture stage beyond stage 0 (up to "
           "%d extra), and this backend reads stage 0 only -- those draws are "
           "MISSING a combiner stage, not missing entirely\n",
           g_multistage_draws, g_multistage_max);
    printf("        texture stage: %lu modulate, %lu select-texture, %lu add "
           "(environment map), %lu other-op-as-modulate, %lu UNTEXTURED (%lu "
           "with no texture bound, %lu with the stage disabled)\n",
           g_texop_modulate, g_texop_select, g_texop_add, g_texop_other,
           g_texop_none_notex + g_texop_none_disabled,
           g_texop_none_notex, g_texop_none_disabled);
}
