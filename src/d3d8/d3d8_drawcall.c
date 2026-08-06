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

/* D3DTSS_*, the ones this reads. */
#define D3DTSS_COLOROP           1
#define D3DTSS_ADDRESSU          13
#define D3DTSS_ADDRESSV          14
#define D3DTSS_MAGFILTER         16
#define D3DTSS_MINFILTER         17

/* D3DTOP_* */
#define D3DTOP_DISABLE           1
#define D3DTOP_SELECTARG1        2
#define D3DTOP_MODULATE          4

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
    if (fvf & D3DFVF_NORMAL)   off += 12;
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
    if (!out->prim) {
        static int told;
        if (!told++)
            fprintf(stderr, "d3d8: primitive type %u (fan, strip-of-lines or "
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

    out->pos_offset = vl.pos_offset;
    out->pretransformed = vl.pretransformed;
    out->color_offset = vl.color_offset;
    out->uv_offset = vl.uv_offset;

    /*
     * The transform.
     *
     * D3D8 keeps world, view and projection separately and multiplies them in
     * that order; the shader wants one matrix. The multiply happens here
     * because it is the engine's convention being honoured, not a rendering
     * decision.
     */
    d3d8_combine_transform(s, out->mvp);

    /* Texture stage 0 only. A second stage is a combiner this shader does not
       have, and it is reported rather than dropped. */
    {
        uint32_t op = s->stage[0][D3DTSS_COLOROP].set
                          ? s->stage[0][D3DTSS_COLOROP].value : D3DTOP_MODULATE;
        if (!req->texture || op == D3DTOP_DISABLE) {
            out->texop = GPU_TEXOP_NONE;
        } else if (op == D3DTOP_SELECTARG1) {
            out->texop = GPU_TEXOP_SELECT_TEXTURE;
        } else if (op == D3DTOP_MODULATE) {
            out->texop = GPU_TEXOP_MODULATE;
        } else {
            static int told;
            if (!told++)
                fprintf(stderr, "d3d8: texture stage operation %u is not one "
                                "this shader implements; the stage is treated "
                                "as MODULATE. Reported once, and it is a "
                                "KNOWN WRONG colour, not a refusal.\n", op);
            out->texop = GPU_TEXOP_MODULATE;
        }
        out->texture = req->texture;
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

void d3d8_drawcall_note_ignored_state(uint32_t which)
{
    if (which < D3D8_MAX_RENDER_STATES) g_ignored[which]++;
}

void d3d8_drawcall_report(void)
{
    if (g_refused_prim)
        printf("        %lu draw(s) refused for an unimplemented primitive "
               "type\n", g_refused_prim);
    if (g_refused_fvf)
        printf("        %lu draw(s) refused for a vertex format this host "
               "cannot express (a real vertex shader, or no position)\n",
               g_refused_fvf);
}
