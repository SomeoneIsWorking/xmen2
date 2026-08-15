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
unsigned long gpu_frame_draws_so_far(void);
#include "d3d8_resource.h"
#include "d3d8_vertex_shader.h"
#include "d3d8_state.h"
#include "d3d8_types.h"

#include "gpu_draw.h"
#include "gpu_device.h"

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>

/* D3DRS_*, the ones this reads. */
#define D3DRS_ZENABLE            7
#define D3DRS_ZWRITEENABLE      14
#define D3DRS_ALPHATESTENABLE   15
#define D3DRS_SRCBLEND          19
#define D3DRS_DESTBLEND         20
#define D3DRS_CULLMODE          22
#define D3DRS_ZFUNC             23
#define D3DRS_ZBIAS             47
#define D3DRS_ALPHAREF          24
#define D3DRS_ALPHAFUNC         25
#define D3DRS_ALPHABLENDENABLE  27
#define D3DRS_LIGHTING         137
#define D3DRS_AMBIENT          139
#define D3DRS_COLORVERTEX      141
#define D3DRS_TEXTUREFACTOR     60

/* D3DTSS_*, the ones this reads. */
#define D3DTOP_SELECTARG2        3
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

/* D3DFVF_* */
/*
 * The position is a 3-BIT FIELD, not a set of flags, and reading it as flags
 * is a defect this file shipped: `fvf & D3DFVF_XYZRHW` is TRUE for XYZB1
 * (0x006) and XYZB5 (0x00e), so a vertex carrying blend weights was decoded as
 * a PRE-TRANSFORMED one -- drawn in screen space, with no lighting and no
 * projection, which flattens a mesh into a plane. XYZB2/B3/B4 fell through to
 * XYZ instead, leaving every attribute after the position short by the weights.
 */
#define D3DFVF_POSITION_MASK  0x000e
#define D3DFVF_XYZ            0x0002
#define D3DFVF_XYZRHW         0x0004
#define D3DFVF_XYZB1          0x0006
#define D3DFVF_XYZB2          0x0008
#define D3DFVF_XYZB3          0x000a
#define D3DFVF_XYZB4          0x000c
#define D3DFVF_XYZB5          0x000e
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
static void light_dump(const GpuDraw *d);
static void light_survey(const GpuDraw *d);
/* The raw D3DCOLOR the engine set, kept so the dump can show what was READ as
   well as what it became -- a zero after conversion and a zero in the register
   are different faults. */
static uint32_t g_last_ambient_raw;
/* Which D3D light INDEX each packed light came from. The dump prints "light 0"
   meaning the first ENABLED one, which is not the index the engine set -- and
   without the index a black light cannot be traced back to the SetLight that
   made it. */
static int g_light_src[8];
/* Where the draw sits in CAMERA space, for the dump. D3D8 defines light
   positions in WORLD space and this stage lights there, so a draw whose world
   position is thousands of units from every light while its camera position is
   a few hundred says the two are not in the same space -- which no colour in
   the dump could ever say. */
static float g_ld_viewpos[3];

static unsigned long g_texop_none_notex, g_texop_none_disabled,
                     g_texop_select, g_texop_select2, g_texop_modulate,
                     g_texop_add, g_texop_other;

/*
 * What the UNTEXTURED draws actually ask their texture stage for.
 *
 * A stage with no texture bound is not necessarily a stage with nothing to
 * say: D3D8 computes COLOROP over its arguments whatever is bound, and
 * SELECTARG1 with D3DTA_TFACTOR or D3DTA_DIFFUSE produces a colour without
 * sampling anything. This backend currently drops the whole combiner the
 * moment no texture is present, so any such draw comes out as the vertex
 * colour -- white, for a vertex format with no diffuse.
 *
 * Whether that matters is a question about THIS game, so it is measured rather
 * than argued: the distinct (op, arg1, arg2) triples seen on an untextured
 * draw, with counts. The table is small and full-ness is reported, because a
 * histogram that silently stopped recording would understate the variety it
 * exists to show.
 */
#define UNTEX_COMBOS 12
static struct { uint32_t op, a1, a2; unsigned long n; } g_untex[UNTEX_COMBOS];
static int g_untex_n;
static unsigned long g_untex_dropped;

static void untex_note(uint32_t op, uint32_t a1, uint32_t a2)
{
    int i;
    for (i = 0; i < g_untex_n; i++)
        if (g_untex[i].op == op && g_untex[i].a1 == a1 && g_untex[i].a2 == a2) {
            g_untex[i].n++;
            return;
        }
    if (g_untex_n == UNTEX_COMBOS) { g_untex_dropped++; return; }
    g_untex[g_untex_n].op = op;
    g_untex[g_untex_n].a1 = a1;
    g_untex[g_untex_n].a2 = a2;
    g_untex[g_untex_n].n = 1;
    g_untex_n++;
}

static const char *ta_name(uint32_t a)
{
    switch (a & 0xFu) {
    case 0: return "DIFFUSE";
    case 1: return "CURRENT";
    case 2: return "TEXTURE";
    case 3: return "TFACTOR";
    default: return "?";
    }
}

static void d3d8_untextured_report(void)
{
    int i;
    unsigned long tot = 0;
    for (i = 0; i < g_untex_n; i++) tot += g_untex[i].n;
    /* Printed even at zero, with its denominator: "no untextured draw wanted
       anything from its stage" and "this was never measured" must differ. */
    printf("        untextured draws, by what their texture stage ASKED for "
           "(%lu draw(s), %d distinct combination(s)%s):\n",
           tot, g_untex_n,
           g_untex_dropped ? ", TABLE FULL -- more exist" : "");
    if (!g_untex_n) {
        printf("          none -- no draw reached this backend with no "
               "texture bound.\n");
        return;
    }
    for (i = 0; i < g_untex_n; i++)
        printf("          COLOROP %2u  ARG1 %-7s ARG2 %-7s  x%lu%s\n",
               g_untex[i].op, ta_name(g_untex[i].a1), ta_name(g_untex[i].a2),
               g_untex[i].n,
               (g_untex[i].op != 1u && (g_untex[i].a1 & 0xFu) != 0u)
                   ? "   <- NOT the vertex colour; this backend draws it as"
                     " one" : "");
}

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

    out->pos_offset = (int)off;
    switch (fvf & D3DFVF_POSITION_MASK) {
    case D3DFVF_XYZ:    off += 12; break;
    case D3DFVF_XYZRHW: out->pretransformed = 1; off += 16; break;
    /*
     * Position plus n blend WEIGHTS. The weights sit between the position and
     * the normal, so they must be stepped over even though nothing here reads
     * them: D3DRS_VERTEXBLEND is never enabled by this title, and with
     * blending disabled D3D8 transforms by world matrix 0 alone -- which is
     * what this backend already does. Skipping them is therefore faithful,
     * while MIS-SIZING them moved the normal and the texture coordinates.
     *
     * The last weight is a packed matrix index rather than a float when
     * D3DFVF_LASTBETA_UBYTE4 is set; it is four bytes either way, so the
     * stride is the same and only the meaning differs.
     */
    case D3DFVF_XYZB1:  off += 12 + 4;  break;
    case D3DFVF_XYZB2:  off += 12 + 8;  break;
    case D3DFVF_XYZB3:  off += 12 + 12; break;
    case D3DFVF_XYZB4:  off += 12 + 16; break;
    case D3DFVF_XYZB5:  off += 12 + 20; break;
    default:
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


/*
 * Which vertex FORMATS this title actually draws with.
 *
 * There was no such census, so "the position field was read as flags" was a
 * defect nobody could size: the fix matters enormously if the engine draws
 * skinned meshes with blend weights and not at all if it never does, and the
 * code could not tell those apart. Every distinct FVF is counted with the
 * position type it decodes to, so the answer is a table rather than an
 * argument.
 */
#define FVF_SEEN_MAX 16
static struct { uint32_t fvf; unsigned long n; } g_fvf_seen[FVF_SEEN_MAX];
static int g_fvf_n;
static unsigned long g_fvf_dropped;

static void fvf_note(uint32_t fvf)
{
    int i;
    for (i = 0; i < g_fvf_n; i++)
        if (g_fvf_seen[i].fvf == fvf) { g_fvf_seen[i].n++; return; }
    if (g_fvf_n == FVF_SEEN_MAX) { g_fvf_dropped++; return; }
    g_fvf_seen[g_fvf_n].fvf = fvf;
    g_fvf_seen[g_fvf_n].n = 1;
    g_fvf_n++;
}

static const char *fvf_position_name(uint32_t fvf)
{
    switch (fvf & D3DFVF_POSITION_MASK) {
    case D3DFVF_XYZ:    return "XYZ";
    case D3DFVF_XYZRHW: return "XYZRHW (pre-transformed)";
    case D3DFVF_XYZB1:  return "XYZB1 (1 blend weight)";
    case D3DFVF_XYZB2:  return "XYZB2 (2 blend weights)";
    case D3DFVF_XYZB3:  return "XYZB3 (3 blend weights)";
    case D3DFVF_XYZB4:  return "XYZB4 (4 blend weights)";
    case D3DFVF_XYZB5:  return "XYZB5 (5 blend weights)";
    default:            return "NO POSITION";
    }
}

static void fvf_report(void)
{
    int i;
    unsigned long tot = 0, blended = 0;
    for (i = 0; i < g_fvf_n; i++) {
        tot += g_fvf_seen[i].n;
        if ((g_fvf_seen[i].fvf & D3DFVF_POSITION_MASK) > D3DFVF_XYZRHW)
            blended += g_fvf_seen[i].n;
    }
    printf("        vertex formats: %lu fixed-function draw(s), %d distinct "
           "FVF(s)%s; %lu of them carry BLEND WEIGHTS\n",
           tot, g_fvf_n, g_fvf_dropped ? " (TABLE FULL -- more exist)" : "",
           blended);
    for (i = 0; i < g_fvf_n; i++)
        printf("          0x%08x  %-26s x%lu\n",
               g_fvf_seen[i].fvf, fvf_position_name(g_fvf_seen[i].fvf),
               g_fvf_seen[i].n);
    if (!g_fvf_n)
        printf("          none -- no fixed-function draw reached this "
               "backend, so this says NOTHING about the vertex formats.\n");
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
/* Draw-time light table vs what SetLight last wrote -- see fill_lighting. */
static unsigned long g_lc_checked, g_lc_differ, g_lc_lost, g_lc_neverset;

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
    g_last_ambient_raw = rs(s, D3DRS_AMBIENT, 0);
    argb_to_rgba(g_last_ambient_raw, out->global_ambient);
    /* The survey has to see the UNLIT draws too, and this early return is why
       it could not: its "0 unlit" was true by construction, not measured -- a
       counter that can only ever print zero. */
    if (!out->lighting) { light_survey(out); return; }

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
        if (out->nlights < 8) g_light_src[out->nlights] = (int)i;
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
        /*
         * Does this draw see what SetLight last wrote for this index?
         *
         * Counted for every enabled light of every draw, and reported with its
         * denominator, because "the engine set it black" and "we lost the
         * colour between SetLight and the draw" look identical in the picture
         * and are completely different defects.
         */
        {
            extern int d3d8_last_setlight_diffuse(unsigned idx, float out[3]);
            float wrote[3];
            if (d3d8_last_setlight_diffuse(i, wrote)) {
                int same = fabsf(wrote[0] - g->diffuse[0]) < 1e-6f
                        && fabsf(wrote[1] - g->diffuse[1]) < 1e-6f
                        && fabsf(wrote[2] - g->diffuse[2]) < 1e-6f;
                int drawblack = g->diffuse[0] == 0.0f && g->diffuse[1] == 0.0f
                             && g->diffuse[2] == 0.0f;
                int wroteblack = wrote[0] == 0.0f && wrote[1] == 0.0f
                              && wrote[2] == 0.0f;
                g_lc_checked++;
                if (!same) g_lc_differ++;
                if (drawblack && !wroteblack) {
                    g_lc_lost++;
                    if (g_lc_lost <= 3)
                        fprintf(stderr, "d3d8: light %u reaches a draw BLACK, "
                                "but the last SetLight for that index wrote "
                                "%.3f %.3f %.3f. The colour is lost between "
                                "the two.\n", i, wrote[0], wrote[1], wrote[2]);
                }
            } else {
                g_lc_neverset++;
            }
        }
        if (g->type == 2 && !told_spot++)
            fprintf(stderr, "d3d8: a SPOT light is enabled; this stage has no "
                            "cone, so it is lit as a point light -- brighter "
                            "outside the cone than the engine asked for.\n");
    }
    {
        float wv[16];
        d3d8_worldview_transform(s, wv);
        g_ld_viewpos[0] = wv[12];
        g_ld_viewpos[1] = wv[13];
        g_ld_viewpos[2] = wv[14];
    }
    light_dump(out);
    light_survey(out);
}


/*
 * X2_LIGHT_DUMP=<n> -- the lighting INPUTS of the first n lit draws.
 *
 * "The level is black and the menu is not" is a statement about numbers this
 * layer computes and never shows. A draw dump says a draw is LIT; it does not
 * say lit BY WHAT. This prints the material, the global ambient and every
 * enabled light, once per draw for the first n, so an all-black scene can be
 * traced to zero lights, a black material, or lights that are real and simply
 * do not reach.
 *
 * It prints the case with NO lights too, loudly, because "lit with zero lights"
 * is the answer that looks most like no output at all.
 */
static long g_ld_want = -2, g_ld_done, g_ld_skip = -1, g_ld_skipped,
            g_ld_qualified;

static void light_dump(const GpuDraw *d)
{
    int i;

    if (g_ld_want == -2) {
        const char *e = getenv("X2_LIGHT_DUMP");
        g_ld_want = (e && *e) ? atol(e) : -1;
    }
    if (g_ld_want <= 0) return;
    if (!d->lighting) return;
    /*
     * The SCENE gate first, when one was asked for. A draw count separates a
     * movie from "some scene" and nothing finer -- it let a menu frame be
     * dumped and three readings had to be retracted. X2_SHOT_AFTER_FILE names
     * the scene by the file the game opens, so the two instruments aim at the
     * same frame and a dump can be held until the level is on screen. */
    {
        extern int k32_file_gate_open(void);
        if (!k32_file_gate_open()) return;
    }
    /*
     * ONLY IN A FRAME THAT IS ALREADY DRAWING A LOT.
     *
     * This gated on a process-lifetime counter alone, and the MENU is lit and
     * submits thousands of lit draws before a level ever loads -- so every
     * dump described the scene that is KNOWN CORRECT, and the conclusions
     * drawn from it ("the material is white", "the world matrix is sane") were
     * about the menu. That is the project's own "cap the boring case, not the
     * interesting one" trap, in its own code.
     *
     * X2_LIGHT_DUMP_MIN=<m> (default 100) requires the CURRENT frame to have
     * already submitted m draws.
     *
     * The default was 300 on the reading that "a menu frame submits ~230 and a
     * level frame ~600". The second half is FALSE for the frames that matter:
     * the tutorial's gameplay frames -- the ones photographed with black
     * characters -- submit 138 to 153 draws (measured, and each kept screenshot
     * now prints its own frame number and draw count next to it). At 300 the
     * only level frames that ever qualified were the busy LOADING ones, which
     * is how a dump of the level still loading came to be written up as a
     * reading about gameplay. The threshold is printed with the first line so a
     * dump of the wrong scene stays visible as one.
     */
    {
        static long minimum = -1;
        static int told;
        if (minimum < 0) {
            const char *e = getenv("X2_LIGHT_DUMP_MIN");
            minimum = (e && *e) ? atol(e) : 100;
        }
        if ((long)gpu_frame_draws_so_far() < minimum) return;
        /* ONCE. This was gated on "nothing dumped yet", which is true for
           every skipped draw as well, so with X2_LIGHT_DUMP_SKIP it printed
           the same line thousands of times. */
        if (!told++)
            fprintf(stderr, "d3d8: X2_LIGHT_DUMP -- only frames that have "
                    "already submitted %ld draw(s) are dumped (set "
                    "X2_LIGHT_DUMP_MIN to change). A menu frame submits far "
                    "fewer, so this is NOT the menu.\n", minimum);
    }
    /*
     * X2_LIGHT_DUMP_SKIP=<n> -- ignore the first n qualifying draws.
     *
     * Without it a dump describes the FIRST lit level frames, which are the
     * ones still loading: the scene gate opens when the game OPENS the level
     * package, not when it finishes building the scene. A reading taken there
     * was written up as "the lights are black in gameplay" and had to be
     * corrected -- the same cap-the-boring-case trap this project keeps
     * finding, one layer along. The number skipped is printed with the first
     * dump so a reading can say which part of the level it describes.
     */
    g_ld_qualified++;
    /* The quota stops the PRINTING, not the counting: a denominator that stops
       growing once the dump is full is not a denominator. */
    if (g_ld_done >= g_ld_want) return;
    {
        if (g_ld_skip < 0) {
            const char *e = getenv("X2_LIGHT_DUMP_SKIP");
            g_ld_skip = (e && *e) ? atol(e) : 0;
        }
        if (g_ld_skipped < g_ld_skip) {
            if (++g_ld_skipped == g_ld_skip)
                fprintf(stderr, "d3d8: X2_LIGHT_DUMP_SKIP -- %ld qualifying "
                        "draw(s) were skipped; what follows is LATER in the "
                        "level, not its first lit frames.\n", g_ld_skip);
            return;
        }
    }
    g_ld_done++;
    fprintf(stderr,
        "d3d8 light dump %ld/%ld at presented frame %lu: %d light(s) enabled, "
        "ambient %.3f %.3f %.3f "
        "(D3DRS_AMBIENT raw 0x%08x), colorvertex %d, has_normal %d\n"
        "    material diffuse %.3f %.3f %.3f  ambient %.3f %.3f %.3f  "
        "emissive %.3f %.3f %.3f\n",
        g_ld_done, g_ld_want, gpu_frames_presented(), d->nlights,
        d->global_ambient[0], d->global_ambient[1], d->global_ambient[2],
        g_last_ambient_raw,
        d->color_vertex, d->normal_offset >= 0,
        d->mat_diffuse[0], d->mat_diffuse[1], d->mat_diffuse[2],
        d->mat_ambient[0], d->mat_ambient[1], d->mat_ambient[2],
        d->mat_emissive[0], d->mat_emissive[1], d->mat_emissive[2]);
    fprintf(stderr,
        "    world row0 %.3f %.3f %.3f %.3f   row3(translation) %.1f %.1f %.1f\n"
        "    the same origin in CAMERA space (world*view): %.1f %.1f %.1f\n"
        "    draw: %u primitive(s), stride %u, texture %u, %s\n",
        d->world[0], d->world[1], d->world[2], d->world[3],
        d->world[12], d->world[13], d->world[14],
        g_ld_viewpos[0], g_ld_viewpos[1], g_ld_viewpos[2],
        d->prim_count, d->vertex_stride, d->texture,
        d->programmable ? "VS" : "FVF");
    if (!d->nlights)
        fprintf(stderr,
            "    NO LIGHT IS ENABLED. With a zero emissive and a zero ambient "
            "this draw can only come out BLACK, whatever its texture.\n");
    for (i = 0; i < d->nlights; i++) {
        const GpuLight *L = &d->light[i];
        float dw[3], dv[3], distw, distv, attw, attv, den;
        fprintf(stderr,
            "    light %d (D3D index %d) type %d diffuse %.3f %.3f %.3f  "
            "amb %.3f %.3f %.3f\n"
            "            pos %.1f %.1f %.1f  dir %.2f %.2f %.2f  range %.1f  "
            "atten %.4f %.6f %.8f\n",
            i, i < 8 ? g_light_src[i] : -1,
            L->type, L->diffuse[0], L->diffuse[1], L->diffuse[2],
            L->ambient[0], L->ambient[1], L->ambient[2],
            L->position[0], L->position[1], L->position[2],
            L->direction[0], L->direction[1], L->direction[2],
            L->range, L->atten[0], L->atten[1], L->atten[2]);
        /*
         * THE ARITHMETIC, both ways, because the numbers above cannot be read
         * by eye. A point light with no constant or linear term is entirely
         * decided by distance: at 5,000 units a quadratic term of 3.78e-5
         * attenuates to about 1/750, which is black, and at 300 units it is
         * about 3.4, which is full brightness clamped. Printing the distance
         * and the attenuation from the draw's WORLD origin and from its CAMERA
         * origin says which space the engine's light positions are in -- and
         * that is a question no colour in this dump can answer.
         */
        if (L->type == 1) {                     /* D3DLIGHT_POINT */
            dw[0] = L->position[0] - d->world[12];
            dw[1] = L->position[1] - d->world[13];
            dw[2] = L->position[2] - d->world[14];
            dv[0] = L->position[0] - g_ld_viewpos[0];
            dv[1] = L->position[1] - g_ld_viewpos[1];
            dv[2] = L->position[2] - g_ld_viewpos[2];
            distw = sqrtf(dw[0]*dw[0] + dw[1]*dw[1] + dw[2]*dw[2]);
            distv = sqrtf(dv[0]*dv[0] + dv[1]*dv[1] + dv[2]*dv[2]);
            den = L->atten[0] + L->atten[1]*distw + L->atten[2]*distw*distw;
            attw = den > 0.0f ? 1.0f / den : 1.0f;
            den = L->atten[0] + L->atten[1]*distv + L->atten[2]*distv*distv;
            attv = den > 0.0f ? 1.0f / den : 1.0f;
            fprintf(stderr,
                "            from this draw's WORLD origin: %.0f units, "
                "attenuation %.4f%s\n"
                "            from its CAMERA origin:        %.0f units, "
                "attenuation %.4f%s\n",
                distw, attw, attw < 0.05f ? "   <- effectively BLACK" : "",
                distv, attv, attv < 0.05f ? "   <- effectively BLACK" : "");
        }
    }
}

/*
 * X2_LIGHT_SURVEY=1 -- WHICH draws of a gameplay frame cannot come out lit,
 * and what those draws have in common.
 *
 * The dump above prints six draws in full and leaves "and the other hundred
 * and forty?" unanswered, which is how a reading taken from one background
 * object came to be written up as a statement about the characters. This
 * classifies EVERY draw of a qualifying frame instead, by an upper bound on
 * what the vertex shader can produce for it:
 *
 *   bound = emissive + mat_ambient*global_ambient
 *         + sum over lights of (mat_ambient*light_ambient
 *                               + diffuse_material*light_diffuse) * atten
 *
 * with N.L taken as 1 -- its largest possible value -- and a vertex-coloured
 * material taken as white. So the bound is generous in every term: a draw
 * whose bound is black is black on this stage NO MATTER where its vertices or
 * normals point, and that is a fact about the draw rather than a sample.
 *
 * The interesting half of the answer is the SPLIT. If every lit draw is black
 * the fault is global (a dead material, a dead light set); if a minority is,
 * those draws are a thing on the screen -- and their common stride, texgen and
 * light set says which thing. The same bound is computed a second time with
 * attenuation forced to 1, which separates "the light colours are zero" from
 * "the lights are too far away", the two candidate causes that the colours
 * alone cannot tell apart.
 */
#define SURVEY_SIGS 8
static int  g_sv_on = -1;
static unsigned long g_sv_frames, g_sv_seen, g_sv_unlit, g_sv_lit, g_sv_black,
                     g_sv_nolights, g_sv_black_noatten, g_sv_vertexcol;
static struct { unsigned stride, texgen, textured, nlights, black_only_atten;
                unsigned long count; } g_sv_sig[SURVEY_SIGS];
static int g_sv_nsig;
static unsigned long g_sv_sig_lost;
static unsigned long g_sv_last_frame = ~0UL;
static int g_sv_started;
static unsigned long g_sv_start_frame, g_sv_ungated;

static float survey_bound(const GpuDraw *d, int ignore_atten)
{
    float acc[3], dm[3];
    int c, i;
    int vertex_material = d->color_vertex && d->color_offset >= 0;

    for (c = 0; c < 3; c++) {
        acc[c] = d->mat_emissive[c] + d->mat_ambient[c] * d->global_ambient[c];
        dm[c] = vertex_material ? 1.0f : d->mat_diffuse[c];
    }
    for (i = 0; i < d->nlights; i++) {
        const GpuLight *L = &d->light[i];
        float atten = 1.0f;
        if (L->type != 3 && !ignore_atten) {    /* not DIRECTIONAL */
            float dx = L->position[0] - d->world[12];
            float dy = L->position[1] - d->world[13];
            float dz = L->position[2] - d->world[14];
            float dist = sqrtf(dx*dx + dy*dy + dz*dz), den;
            if (dist > L->range) continue;      /* the shader drops it too */
            den = L->atten[0] + L->atten[1]*dist + L->atten[2]*dist*dist;
            atten = den > 0.0f ? 1.0f / den : 1.0f;
        }
        for (c = 0; c < 3; c++)
            acc[c] += (d->mat_ambient[c] * L->ambient[c]
                       + dm[c] * L->diffuse[c]) * atten;
    }
    for (c = 0; c < 3; c++) if (acc[c] > 1.0f) acc[c] = 1.0f;
    return 0.299f*acc[0] + 0.587f*acc[1] + 0.114f*acc[2];
}

static void light_survey(const GpuDraw *d)
{
    float bound, bound_noatten;
    int black, only_atten, i;

    if (g_sv_on < 0) {
        const char *e = getenv("X2_LIGHT_SURVEY");
        g_sv_on = (e && *e) ? atoi(e) : 0;
    }
    if (!g_sv_on) return;
    {
        /*
         * The level is open, and gameplay has STARTED -- which is not the same
         * gate the dump uses, and the difference invalidated this survey's
         * first reading.
         *
         * The dump's threshold is a property of the DRAW ("this frame has
         * already submitted 100"), which is right for picking a specimen and
         * wrong for a census: it silently drops the first 100 draws of every
         * frame, so a survey gated that way described the last 20 draws of a
         * 140-draw frame and printed 421 as though it were the denominator.
         * The characters are drawn somewhere in a frame, and "somewhere" is
         * exactly what a survey may not assume.
         *
         * So the threshold opens the gate ONCE, for good: the first frame to
         * reach it says gameplay is running, and every draw of every frame
         * from then on is counted. What is missed is then bounded and known --
         * the frames before that point -- rather than an unstated slice of
         * every frame.
         */
        extern int k32_file_gate_open(void);
        static long minimum = -1;
        if (!k32_file_gate_open()) { g_sv_ungated++; return; }
        if (minimum < 0) {
            const char *e = getenv("X2_LIGHT_DUMP_MIN");
            minimum = (e && *e) ? atol(e) : 100;
        }
        if (!g_sv_started) {
            if ((long)gpu_frame_draws_so_far() < minimum) {
                g_sv_ungated++;
                return;
            }
            g_sv_started = 1;
            g_sv_start_frame = gpu_frames_presented();
            fprintf(stderr, "[SURVEY] gameplay reached at presented frame "
                    "%lu (a frame submitted %ld draws); EVERY draw from here "
                    "on is classified. %lu draw(s) before this point were "
                    "not.\n",
                    g_sv_start_frame, minimum, g_sv_ungated);
        }
    }
    if (g_sv_last_frame != gpu_frames_presented()) {
        g_sv_last_frame = gpu_frames_presented();
        g_sv_frames++;
        /*
         * LIVE, not only at shutdown.
         *
         * Nothing here stops on its own and an interactive run ends when the
         * window is closed, so a classification that exists only in the
         * shutdown report is a classification nobody reads. The first gated
         * frame prints, then every X2_LIGHT_SURVEY_EVERY (default 120) --
         * which is also what makes the number WATCHABLE: if the count of
         * bounded-black draws moves when the player moves or switches
         * character, that is the symptom, live, tied to what is on screen.
         */
        {
            static long every = -1;
            if (every < 0) {
                const char *e = getenv("X2_LIGHT_SURVEY_EVERY");
                every = (e && *e) ? atol(e) : 120;
                if (every < 1) every = 1;
            }
            if (g_sv_frames == 1 || g_sv_frames % (unsigned long)every == 0)
                fprintf(stderr,
                    "[SURVEY] frame %lu: of %lu lit draw(s) so far, %lu are "
                    "bounded BLACK (%lu of those by distance alone); %lu "
                    "unlit; %lu with no light enabled\n",
                    gpu_frames_presented(), g_sv_lit, g_sv_black,
                    g_sv_black_noatten, g_sv_unlit, g_sv_nolights);
        }
    }
    g_sv_seen++;
    if (!d->lighting) { g_sv_unlit++; return; }
    g_sv_lit++;
    if (d->color_vertex && d->color_offset >= 0) g_sv_vertexcol++;
    if (!d->nlights) g_sv_nolights++;

    bound = survey_bound(d, 0);
    bound_noatten = survey_bound(d, 1);
    black = bound < 0.02f;
    only_atten = black && bound_noatten >= 0.02f;
    if (!black) return;
    g_sv_black++;
    if (only_atten) g_sv_black_noatten++;

    for (i = 0; i < g_sv_nsig; i++)
        if (g_sv_sig[i].stride == d->vertex_stride
            && g_sv_sig[i].texgen == (unsigned)d->texgen
            && g_sv_sig[i].textured == (d->texture != 0)
            && g_sv_sig[i].nlights == (unsigned)d->nlights
            && g_sv_sig[i].black_only_atten == (unsigned)only_atten) {
            g_sv_sig[i].count++;
            return;
        }
    if (g_sv_nsig == SURVEY_SIGS) { g_sv_sig_lost++; return; }
    g_sv_sig[g_sv_nsig].stride = d->vertex_stride;
    g_sv_sig[g_sv_nsig].texgen = (unsigned)d->texgen;
    g_sv_sig[g_sv_nsig].textured = (d->texture != 0);
    g_sv_sig[g_sv_nsig].nlights = (unsigned)d->nlights;
    g_sv_sig[g_sv_nsig].black_only_atten = (unsigned)only_atten;
    g_sv_sig[g_sv_nsig].count = 1;
    g_sv_nsig++;
}

static void light_survey_report(void)
{
    int i;
    if (g_sv_on <= 0) return;
    printf("        X2_LIGHT_SURVEY: %lu draw(s) over %lu gameplay frame(s) "
           "-- EVERY draw submitted from presented frame %lu on; %lu unlit, "
           "%lu lit\n",
           g_sv_seen, g_sv_frames, g_sv_start_frame, g_sv_unlit, g_sv_lit);
    if (!g_sv_seen) {
        printf("          NO DRAW WAS EVER SURVEYED -- gameplay was never "
               "reached (%lu draw(s) went by before the level-open gate or "
               "before any frame submitted enough draws). This says NOTHING "
               "about the lighting.\n", g_sv_ungated);
        return;
    }
    printf("          %lu draw(s) before gameplay started are not in this "
           "count\n", g_sv_ungated);
    printf("          of the %lu lit: %lu cannot come out brighter than black "
           "even with N.L=1, %lu of those ONLY because of distance "
           "attenuation (they would light at atten=1), %lu have no light "
           "enabled at all, %lu take their diffuse from the vertex\n",
           g_sv_lit, g_sv_black, g_sv_black_noatten, g_sv_nolights,
           g_sv_vertexcol);
    if (!g_sv_black)
        printf("          NOT ONE lit draw is bounded black: whatever is dark "
               "on screen is not this stage's lighting arithmetic.\n");
    for (i = 0; i < g_sv_nsig; i++)
        printf("          black x%lu: stride %u, texgen %u, %s, %u light(s)%s\n",
               g_sv_sig[i].count, g_sv_sig[i].stride, g_sv_sig[i].texgen,
               g_sv_sig[i].textured ? "textured" : "NO texture",
               g_sv_sig[i].nlights,
               g_sv_sig[i].black_only_atten ? " -- distance alone" : "");
    if (g_sv_sig_lost)
        printf("          %lu black draw(s) had a %dth distinct signature and "
               "are counted above but not described.\n",
               g_sv_sig_lost, SURVEY_SIGS + 1);
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
 * Does this draw read OUTSIDE the vertex buffer it is bound to?
 *
 * Nothing asked this question before, and a GPU page fault is what asking it
 * costs to miss: amdgpu killed a run with
 *
 *   [gfxhub] page fault ... Process x2native ... client 0x1b (UTCL2)
 *   ring gfx_0.0.0 timeout ... Ring gfx_0.0.0 reset succeeded
 *
 * A vertex fetch past the end of a buffer is the ordinary way to produce that,
 * and an index buffer whose contents outrun the stream bound beside it is the
 * ordinary way to produce THAT. D3D8 lets the guest set the two independently,
 * so the pairing is only wrong at the draw -- which is here.
 *
 * The check is exact, not a heuristic: the indices live in guest memory (they
 * are uploaded from there on Unlock), so the largest one this draw will
 * actually read is a fact available on the CPU. Reading them is O(indices) per
 * indexed draw, which is worth it against resetting the GPU.
 *
 * Every outcome is counted, INCLUDING the one where the check could not run --
 * an indexed draw whose index buffer has no guest storage cannot be verified,
 * and "not verified" must never be filed under "fine".
 */
static unsigned long g_rng_checked, g_rng_unverifiable, g_rng_bad;
static uint32_t g_rng_worst_need, g_rng_worst_have;

static uint32_t index_count_of(uint32_t prim_type, uint32_t prim_count)
{
    switch (prim_type) {
    case D3DPT_POINTLIST:     return prim_count;
    case D3DPT_LINELIST:      return prim_count * 2u;
    case D3DPT_LINESTRIP:     return prim_count + 1u;
    case D3DPT_TRIANGLELIST:  return prim_count * 3u;
    case D3DPT_TRIANGLESTRIP:
    case D3DPT_TRIANGLEFAN:   return prim_count + 2u;
    default:                  return 0;
    }
}

static int draw_range_ok(const D3D8DrawRequest *req, uint32_t stride)
{
    uint32_t n = index_count_of(req->primitive_type, req->primitive_count);
    uint32_t have, need = 0, i, maxi = 0;

    if (!stride || !n) return 1;              /* nothing this can decide */
    have = req->vertex_bytes / stride;

    if (req->index_buffer) {
        uint32_t esz = req->index_is_32bit ? 4u : 2u;
        uint64_t last = (uint64_t)(req->first_index + n) * esz;
        if (!req->index_guest_bytes || last > req->index_bytes) {
            /*
             * FAIL FAST: a draw whose index range cannot be checked is
             * REFUSED, not waved through.
             *
             * This used to return 1 -- "unverifiable, carry on" -- which is
             * how an unverified draw reaches the GPU and, if its indices do
             * run past the stream, page-faults the device and resets the card
             * for every process on it. Measured over a full gameplay run:
             * 0 of 273,289 draws land here, so refusing costs nothing today
             * and turns a future silent risk into a visible hole with a line
             * of log next to it.
             */
            g_rng_unverifiable++;
            if (g_rng_unverifiable <= 3)
                fprintf(stderr, "d3d8: an indexed draw's range cannot be "
                        "checked (index guest 0x%08x, %u byte(s), first index "
                        "%u, %u indices needed) -- REFUSED rather than "
                        "submitted unverified.\n",
                        req->index_guest_bytes, req->index_bytes,
                        req->first_index, n);
            return 0;
        }
        if (req->index_is_32bit) {
            const uint32_t *p = (const uint32_t *)(uintptr_t)
                                req->index_guest_bytes + req->first_index;
            for (i = 0; i < n; i++) if (p[i] > maxi) maxi = p[i];
        } else {
            const uint16_t *p = (const uint16_t *)(uintptr_t)
                                req->index_guest_bytes + req->first_index;
            for (i = 0; i < n; i++) if (p[i] > maxi) maxi = p[i];
        }
        need = req->base_vertex + maxi + 1u;
    } else {
        need = req->first_vertex + n;
    }
    g_rng_checked++;
    if (need <= have) return 1;

    g_rng_bad++;
    if (need - have > g_rng_worst_need - g_rng_worst_have ||
        !g_rng_worst_need) {
        g_rng_worst_need = need;
        g_rng_worst_have = have;
    }
    if (g_rng_bad <= 4)
        fprintf(stderr,
            "d3d8: a draw would fetch vertex %u from a stream holding %u "
            "(%u bytes at stride %u) -- REFUSED. primitive type %u, %u "
            "primitive(s), base vertex %u, first index %u. Submitting it "
            "reads outside the buffer, which is how the GPU is made to page "
            "fault.\n",
            need - 1u, have, req->vertex_bytes, stride,
            req->primitive_type, req->primitive_count,
            req->base_vertex, req->first_index);
    return 0;
}

/*
 * X2_FRAME_TABLE=1 -- every draw of one gameplay frame, and WHERE ON SCREEN it
 * lands.
 *
 * "Cyclops's head is collapsed and black" is a statement about pixels, and
 * every instrument in this file describes DRAWS. Bridging the two by bisecting
 * with X2_DRAW_RANGE costs one nine-minute run per bisection step, and guessing
 * which draw is a character from its stride has already produced one retracted
 * reading (C193).
 *
 * So: project this draw's vertices through its own mvp, on the CPU, exactly as
 * the vertex stage will, and print the screen rectangle they cover. A pixel on
 * the screenshot then names the draws that could have painted it, and their
 * format, texture and lighting come with it. Only the vertices this draw
 * actually INDEXES are projected, because a shared buffer's other vertices are
 * somewhere else entirely.
 *
 * One frame, once: the table is the whole point, and a table repeated 3,000
 * times is a log nobody reads.
 */
static int g_ft_on = -1, g_ft_done;
static unsigned long g_ft_frame, g_ft_draw;

static void frame_table_note(const D3D8DrawRequest *req, const GpuDraw *out,
                             uint32_t stride, int pos_offset, int normal_offset,
                             uint32_t fvf)
{
    float minx = 1e30f, miny = 1e30f, maxx = -1e30f, maxy = -1e30f;
    float minz = 1e30f, maxz = -1e30f;
    /* OBJECT space too. The screen rectangle needs a divide by w and goes to
       infinity as w approaches zero, so it cannot answer "is this mesh flat".
       The object-space extents can: a head collapsed to a plane has one extent
       at or near zero, and that is a property of the vertex DATA rather than
       of any matrix. */
    float omin[3] = { 1e30f, 1e30f, 1e30f }, omax[3] = { -1e30f, -1e30f, -1e30f };
    uint32_t n, i, capacity, behind = 0, used = 0, nearw = 0;
    /*
     * THE NORMALS, because the light survey cannot see them.
     *
     * That survey bounds a draw's colour with N.L forced to 1 -- its largest
     * possible value -- which is exactly what makes it blind to a mesh that is
     * black because its normals are wrong. A zero-length normal gives N.L = 0
     * for every light and renders the surface at ambient alone, which is black
     * in this game's interiors; so does a normal that points away from every
     * light. One model can be ruined this way while the room around it, whose
     * normals are fine, renders correctly -- which is the reported symptom.
     */
    double nsum = 0.0;
    uint32_t nzero = 0, ncount = 0;
    /* 240, not 80: at 80 this line truncated mid-word ("best light: t") and
       the three numbers the measurement existed to show were silently cut
       off. snprintf does not complain, so the buffer has to be right. */
    char nphrase[400];
    /* WHICH D3D light indices this draw has, and how bright each is. The
       engine sets a white directional light at index 0; if that light is not
       in this draw's enabled set, the model is dark for a reason that has
       nothing to do with the lighting arithmetic. */
    char lphrase[200];
    /*
     * WHAT THE SHADER WILL ACTUALLY OUTPUT for this draw.
     *
     * Every input has now been measured correct for a model that still renders
     * black -- unit normals, a textured stage, lights whose colours and
     * attenuation cannot produce black, a world matrix that puts the mesh in
     * the right place. So stop testing inputs and compute the OUTPUT: the same
     * arithmetic src/gpu/shaders/d3d8_fixed.vert runs, on this draw's real
     * normals, real lights and real world matrix.
     *
     * This is the number the light survey deliberately would not compute: the
     * survey forces N.L to 1 so its verdict is an upper BOUND that no geometry
     * can escape, which is what makes it blind to a mesh whose normals point
     * away from every light. Here N.L is the real one.
     */
    double litsum = 0.0, litraw = 0.0, bestnl = -2.0;
    uint32_t litn = 0;
    /*
     * The world matrix's SCALE, and the same lighting computed WITHOUT
     * normalising the transformed normal.
     *
     * D3D8 normalises the world-space normal only when D3DRS_NORMALIZENORMALS
     * is TRUE. This title sets it FALSE (it is in the set-but-unread list), so
     * a mesh whose world matrix carries a scale is lit by a normal of that
     * length -- brighter for a scale above 1 -- while this backend's shader
     * normalises unconditionally and lights it dimmer. If the dark models are
     * the SCALED ones, that difference is the bug; if their scale is 1, this
     * measurement kills the theory instead.
     */
    double wscale;
    double bestcontrib = 0.0, bestatten = 0.0, bestldiff = 0.0;
    int bestlt = -1;
    const uint8_t *vb;

    if (g_ft_on < 0) {
        const char *e = getenv("X2_FRAME_TABLE");
        g_ft_on = (e && *e) ? atoi(e) : 0;
    }
    if (!g_ft_on || g_ft_done) return;
    {   /* The same gameplay gate the survey uses, so the table describes a
           frame with the level on screen rather than a menu. */
        extern int k32_file_gate_open(void);
        static long minimum = -1;
        if (!k32_file_gate_open()) return;
        if (minimum < 0) {
            const char *e = getenv("X2_LIGHT_DUMP_MIN");
            minimum = (e && *e) ? atol(e) : 100;
        }
        if (!g_ft_frame) {
            if ((long)gpu_frame_draws_so_far() < minimum) return;
            g_ft_frame = gpu_frames_presented();
            fprintf(stderr, "[FRAME TABLE] presented frame %lu -- every draw "
                    "of THIS frame, with the screen rectangle its vertices "
                    "cover, in pixels of an 800x600 target.\n", g_ft_frame);
        } else if (gpu_frames_presented() != g_ft_frame) {
            g_ft_done = 1;
            fprintf(stderr, "[FRAME TABLE] end of frame %lu -- %lu draw(s) "
                    "listed.\n", g_ft_frame, g_ft_draw);
            return;
        }
    }
    g_ft_draw++;
    wscale = sqrt((double)out->world[0]*out->world[0] +
                  (double)out->world[1]*out->world[1] +
                  (double)out->world[2]*out->world[2]);

    if (!stride || pos_offset < 0 || !req->vertex_guest_bytes) {
        fprintf(stderr, "  draw %-4lu NO host-readable vertices (guest 0x%08x "
                "stride %u) -- position unknown\n",
                g_ft_draw, req->vertex_guest_bytes, stride);
        return;
    }
    vb = (const uint8_t *)(uintptr_t)req->vertex_guest_bytes;
    capacity = req->vertex_bytes / stride;
    n = index_count_of(req->primitive_type, req->primitive_count);

    for (i = 0; i < n; i++) {
        uint32_t v;
        const float *p;
        float x, y, z, w;
        if (req->index_buffer && req->index_guest_bytes) {
            if (req->index_is_32bit)
                v = ((const uint32_t *)(uintptr_t)req->index_guest_bytes)
                        [req->first_index + i] + req->base_vertex;
            else
                v = ((const uint16_t *)(uintptr_t)req->index_guest_bytes)
                        [req->first_index + i] + req->base_vertex;
        } else {
            v = req->first_vertex + i;
        }
        if (v >= capacity) continue;
        p = (const float *)(vb + (size_t)v * stride + pos_offset);
        /* Row-vector convention, matching the shader's `mvp * vec4(pos,1)`
           with the matrix handed over as D3D lays it out. */
        x = p[0]*out->mvp[0] + p[1]*out->mvp[4] + p[2]*out->mvp[8]  + out->mvp[12];
        y = p[0]*out->mvp[1] + p[1]*out->mvp[5] + p[2]*out->mvp[9]  + out->mvp[13];
        z = p[0]*out->mvp[2] + p[1]*out->mvp[6] + p[2]*out->mvp[10] + out->mvp[14];
        w = p[0]*out->mvp[3] + p[1]*out->mvp[7] + p[2]*out->mvp[11] + out->mvp[15];
        if (normal_offset >= 0) {
            const float *q = (const float *)(vb + (size_t)v * stride
                                             + normal_offset);
            double len = sqrt((double)q[0]*q[0] + (double)q[1]*q[1]
                              + (double)q[2]*q[2]);
            nsum += len;
            ncount++;
            if (len < 1e-4) nzero++;
            /* Sampled, not every vertex: 16 spread across the draw is enough
               to tell a lit surface from a black one, and this runs on the
               CPU for every vertex of every draw of the frame otherwise. */
            if (out->lighting && len > 1e-4 && litn < 16u &&
                (n < 16u || (i % (n / 16u)) == 0u)) {
                double wn[3], acc[3], sc;
                int c, li;
                /* The normal by the world matrix's upper 3x3, as the shader
                   does it, then normalised. */
                wn[0] = q[0]*out->world[0] + q[1]*out->world[4] + q[2]*out->world[8];
                wn[1] = q[0]*out->world[1] + q[1]*out->world[5] + q[2]*out->world[9];
                wn[2] = q[0]*out->world[2] + q[1]*out->world[6] + q[2]*out->world[10];
                sc = sqrt(wn[0]*wn[0] + wn[1]*wn[1] + wn[2]*wn[2]);
                if (sc > 1e-6) { wn[0] /= sc; wn[1] /= sc; wn[2] /= sc; }
                for (c = 0; c < 3; c++)
                    acc[c] = out->mat_emissive[c]
                           + out->mat_ambient[c] * out->global_ambient[c];
                for (li = 0; li < out->nlights; li++) {
                    const GpuLight *L = &out->light[li];
                    double tl[3], nl, atten = 1.0, d2;
                    if (L->type == 3) {          /* DIRECTIONAL */
                        d2 = sqrt((double)L->direction[0]*L->direction[0] +
                                  (double)L->direction[1]*L->direction[1] +
                                  (double)L->direction[2]*L->direction[2]);
                        if (d2 < 1e-6) continue;
                        tl[0] = -L->direction[0]/d2;
                        tl[1] = -L->direction[1]/d2;
                        tl[2] = -L->direction[2]/d2;
                    } else {
                        double wp[3], dd[3], dist, den;
                        wp[0] = p[0]*out->world[0] + p[1]*out->world[4]
                              + p[2]*out->world[8]  + out->world[12];
                        wp[1] = p[0]*out->world[1] + p[1]*out->world[5]
                              + p[2]*out->world[9]  + out->world[13];
                        wp[2] = p[0]*out->world[2] + p[1]*out->world[6]
                              + p[2]*out->world[10] + out->world[14];
                        dd[0] = L->position[0]-wp[0];
                        dd[1] = L->position[1]-wp[1];
                        dd[2] = L->position[2]-wp[2];
                        dist = sqrt(dd[0]*dd[0]+dd[1]*dd[1]+dd[2]*dd[2]);
                        if (dist > L->range || dist <= 0.0) continue;
                        tl[0]=dd[0]/dist; tl[1]=dd[1]/dist; tl[2]=dd[2]/dist;
                        den = L->atten[0] + L->atten[1]*dist
                            + L->atten[2]*dist*dist;
                        atten = den > 0.0 ? 1.0/den : 1.0;
                    }
                    nl = wn[0]*tl[0] + wn[1]*tl[1] + wn[2]*tl[2];
                    if (nl > bestnl) bestnl = nl;
                    if (nl < 0.0) nl = 0.0;
                    for (c = 0; c < 3; c++)
                        acc[c] += (out->mat_ambient[c]*L->ambient[c]
                                   + out->mat_diffuse[c]*L->diffuse[c]*nl)
                                  * atten;
                    /* WHICH light contributes most, and what each factor of
                       its contribution is. With N.L at 1.0 and the result
                       still at 0.08, the answer is in one of these three
                       numbers and nothing else. */
                    {
                        double contrib = (0.299*out->mat_diffuse[0]*L->diffuse[0]
                                        + 0.587*out->mat_diffuse[1]*L->diffuse[1]
                                        + 0.114*out->mat_diffuse[2]*L->diffuse[2])
                                        * nl * atten;
                        if (contrib > bestcontrib) {
                            bestcontrib = contrib;
                            bestlt = L->type;
                            bestatten = atten;
                            bestldiff = 0.299*L->diffuse[0] + 0.587*L->diffuse[1]
                                      + 0.114*L->diffuse[2];
                        }
                    }
                }
                for (c = 0; c < 3; c++) if (acc[c] > 1.0) acc[c] = 1.0;
                litsum += 0.299*acc[0] + 0.587*acc[1] + 0.114*acc[2];
                /* Again, with the UNNORMALISED world normal -- what D3D8 uses
                   when NORMALIZENORMALS is false. Only the N.L term changes. */
                {
                    double rn[3], racc[3];
                    int c2, li2;
                    rn[0] = q[0]*out->world[0] + q[1]*out->world[4] + q[2]*out->world[8];
                    rn[1] = q[0]*out->world[1] + q[1]*out->world[5] + q[2]*out->world[9];
                    rn[2] = q[0]*out->world[2] + q[1]*out->world[6] + q[2]*out->world[10];
                    for (c2 = 0; c2 < 3; c2++)
                        racc[c2] = out->mat_emissive[c2]
                                 + out->mat_ambient[c2]*out->global_ambient[c2];
                    for (li2 = 0; li2 < out->nlights; li2++) {
                        const GpuLight *L = &out->light[li2];
                        double tl[3], nl, d2;
                        if (L->type != 3) continue;      /* directional only */
                        d2 = sqrt((double)L->direction[0]*L->direction[0] +
                                  (double)L->direction[1]*L->direction[1] +
                                  (double)L->direction[2]*L->direction[2]);
                        if (d2 < 1e-6) continue;
                        tl[0] = -L->direction[0]/d2;
                        tl[1] = -L->direction[1]/d2;
                        tl[2] = -L->direction[2]/d2;
                        nl = rn[0]*tl[0] + rn[1]*tl[1] + rn[2]*tl[2];
                        if (nl < 0.0) nl = 0.0;
                        for (c2 = 0; c2 < 3; c2++)
                            racc[c2] += out->mat_ambient[c2]*L->ambient[c2]
                                      + out->mat_diffuse[c2]*L->diffuse[c2]*nl;
                    }
                    for (c2 = 0; c2 < 3; c2++)
                        if (racc[c2] > 1.0) racc[c2] = 1.0;
                    litraw += 0.299*racc[0] + 0.587*racc[1] + 0.114*racc[2];
                }
                litn++;
            }
        }
        if (p[0] < omin[0]) omin[0] = p[0];
        if (p[0] > omax[0]) omax[0] = p[0];
        if (p[1] < omin[1]) omin[1] = p[1];
        if (p[1] > omax[1]) omax[1] = p[1];
        if (p[2] < omin[2]) omin[2] = p[2];
        if (p[2] > omax[2]) omax[2] = p[2];
        if (w <= 0.0f) { behind++; continue; }
        /* A vertex almost ON the near plane projects to a coordinate in the
           hundreds of thousands, which would make the rectangle meaningless
           rather than merely large. Counted and excluded, never silently
           folded in. */
        if (w < 1e-3f) { nearw++; continue; }
        x /= w; y /= w; z /= w;
        used++;
        if (x < minx) minx = x;
        if (x > maxx) maxx = x;
        if (y < miny) miny = y;
        if (y > maxy) maxy = y;
        if (z < minz) minz = z;
        if (z > maxz) maxz = z;
    }
    if (!used) {
        fprintf(stderr, "  draw %-4lu %5u prim  stride %-3u tex %-3u  "
                "ALL %u vertices behind the camera or out of range\n",
                g_ft_draw, out->prim_count, stride, out->texture, n);
        return;
    }
    {
        int li3;
        size_t at = 0;
        lphrase[0] = 0;
        for (li3 = 0; li3 < out->nlights && at + 24 < sizeof lphrase; li3++) {
            const GpuLight *L = &out->light[li3];
            at += (size_t)snprintf(lphrase + at, sizeof lphrase - at,
                       "%s#%d t%d %.2f", li3 ? ", " : "",
                       li3 < 8 ? g_light_src[li3] : -1, L->type,
                       0.299*L->diffuse[0] + 0.587*L->diffuse[1]
                       + 0.114*L->diffuse[2]);
        }
        if (!out->nlights) snprintf(lphrase, sizeof lphrase, "NONE");
    }
    /*
     * "This format HAS no normal" and "its normals measure zero" must not
     * print the same. They did -- every FVF without a normal bit reported
     * `|N| 0.000`, which read as 31 draws with dead normals and was nothing of
     * the kind. The phrase says which case it is.
     */
    {
        if (normal_offset < 0)
            snprintf(nphrase, sizeof nphrase, "no normal in this FVF");
        else if (!ncount)
            snprintf(nphrase, sizeof nphrase, "normal at +%d, NO vertex read",
                     normal_offset);
        else if (litn)
            snprintf(nphrase, sizeof nphrase,
                     "|N| %.2f scale %.2f LIT %.3f (N.L %.2f) nlights %d "
                     "matdiff %.2f amb %.2f emis %.2f | best light: type %d "
                     "diffuse %.2f atten %.4f -> %.3f | enabled: %s",
                     nsum / ncount, wscale, litsum / litn, bestnl,
                     out->nlights,
                     0.299*out->mat_diffuse[0] + 0.587*out->mat_diffuse[1]
                     + 0.114*out->mat_diffuse[2],
                     0.299*out->mat_ambient[0] + 0.587*out->mat_ambient[1]
                     + 0.114*out->mat_ambient[2],
                     0.299*out->mat_emissive[0] + 0.587*out->mat_emissive[1]
                     + 0.114*out->mat_emissive[2],
                     bestlt, bestldiff, bestatten, bestcontrib, lphrase);
        else
            snprintf(nphrase, sizeof nphrase, "|N| %.3f over %u vertex(es), "
                     "%.0f%% zero-length", nsum / ncount, ncount,
                     100.0 * nzero / ncount);
    }
    /* NDC (-1..1, y down in this pipeline) to pixels. */
    fprintf(stderr,
        "  draw %-4lu %5u prim  stride %-3u tex %-3u  fvf 0x%05x  lit %d "
        "texgen %d  screen x %5.0f..%-5.0f y %5.0f..%-5.0f  z %.3f..%.3f  "
        "object extent %.1f x %.1f x %.1f  %s%s%s%s\n",
        g_ft_draw, out->prim_count, stride, out->texture,
        fvf, out->lighting, (int)out->texgen,
        (minx*0.5f+0.5f)*800.0f, (maxx*0.5f+0.5f)*800.0f,
        (miny*0.5f+0.5f)*600.0f, (maxy*0.5f+0.5f)*600.0f,
        minz, maxz,
        omax[0]-omin[0], omax[1]-omin[1], omax[2]-omin[2], nphrase,
        (omax[0]-omin[0] < 0.01f || omax[1]-omin[1] < 0.01f ||
         omax[2]-omin[2] < 0.01f) ? "  <- FLAT in one axis" : "",
        behind ? "  (some behind the camera)" : "",
        nearw ? "  (some on the near plane)" : "");
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
    int programmable = fvf > 0xf0000000u;

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
    if (!programmable) fvf_note(fvf);
    if (!programmable && !d3d8_fvf_layout(fvf, &vl)) {
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

    /* BEFORE the fan expansion, which rewrites the indices into a buffer of
       its own: what has to be checked is what the guest asked for. */
    if (!draw_range_ok(req, out->vertex_stride)) return 0;

    if (req->primitive_type == D3DPT_TRIANGLEFAN && !fan_expand(req, out)) {
        g_refused_prim++;
        return 0;
    }

    if (programmable) {
        D3D8VSOutput *vertices;
        uint32_t count, bytes;
        if (!req->vertex_guest_bytes || !req->vertex_bytes || !req->stride) {
            fprintf(stderr, "d3d8: programmable draw has no host-visible "
                            "stream-0 bytes (guest=0x%08x bytes=%u stride=%u).\n",
                    req->vertex_guest_bytes, req->vertex_bytes, req->stride);
            g_refused_fvf++; return 0;
        }
        count = req->vertex_bytes / req->stride;
        if (!count || count > UINT32_MAX / sizeof *vertices) {
            fprintf(stderr, "d3d8: programmable draw derives %u vertices "
                            "from %u bytes at stride %u.\n",
                    count, req->vertex_bytes, req->stride);
            g_refused_fvf++; return 0;
        }
        bytes = count * (uint32_t)sizeof *vertices;
        vertices = malloc(bytes);
        if (!vertices) { g_refused_fvf++; return 0; }
        if (!d3d8_vs_execute(fvf, s->vertex_shader_constant,
                (const void *)(uintptr_t)req->vertex_guest_bytes,
                req->vertex_bytes, req->stride, 0, count, vertices)) {
            free(vertices); g_refused_fvf++; return 0;
        }
        out->vertices = gpu_buffer_create(GPU_BUF_VERTEX, bytes);
        if (!out->vertices
                || !gpu_buffer_upload(out->vertices, 0, vertices, bytes)) {
            if (out->vertices) gpu_buffer_destroy(out->vertices);
            out->vertices = 0;
            free(vertices); g_refused_fvf++; return 0;
        }
        free(vertices);
        out->owns_vertices = 1;
        out->vertex_stride = sizeof(D3D8VSOutput);
        out->pos_offset = offsetof(D3D8VSOutput, position);
        out->pretransformed = 0;
        out->programmable = 1;
        out->color_offset = offsetof(D3D8VSOutput, diffuse);
        out->uv_offset = offsetof(D3D8VSOutput, texcoord);
        out->normal_offset = -1;
    } else {
        out->pos_offset = vl.pos_offset;
        out->pretransformed = vl.pretransformed;
        out->color_offset = vl.color_offset;
        out->uv_offset = vl.uv_offset;
        out->normal_offset = vl.normal_offset;
    }

    /*
     * The transform.
     *
     * D3D8 keeps world, view and projection separately and multiplies them in
     * that order; the shader wants one matrix. The multiply happens here
     * because it is the engine's convention being honoured, not a rendering
     * decision.
     */
    d3d8_combine_transform(s, out->mvp);
    if (!programmable) fill_lighting(s, out);

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
        /*
         * NO TEXTURE IS NOT NO COMBINER.
         *
         * This used to bail to GPU_TEXOP_NONE -- "the vertex colour is the
         * result" -- the moment nothing was bound, and that is wrong for any
         * stage whose selected arguments do not include the texture. D3D8
         * computes COLOROP over its arguments regardless of what is bound, so
         * SELECTARG2 with D3DTA_TFACTOR produces the texture-factor colour and
         * samples nothing.
         *
         * MEASURED on a menu run: 1,070 draws do exactly that (COLOROP 3,
         * ARG2 TFACTOR), and every one of them came out as the vertex colour.
         * For the sky dome -- FVF D3DFVF_XYZ, no diffuse -- the vertex colour
         * is WHITE, which is the white sky.
         *
         * A stage that IS disabled, and one whose chosen argument really is
         * the texture with nothing bound, still resolve to the vertex colour;
         * the second is D3D8-undefined and this is the answer already
         * documented for it.
         */
        {
        uint32_t ca1 = s->stage[0][D3DTSS_COLORARG1].set
                           ? s->stage[0][D3DTSS_COLORARG1].value : 2u;
        uint32_t ca2 = s->stage[0][D3DTSS_COLORARG2].set
                           ? s->stage[0][D3DTSS_COLORARG2].value : 1u;
        /* Which argument the op actually reads decides whether a missing
           texture matters at all. */
        int needs_tex =
            op == D3DTOP_SELECTARG1 ? (ca1 & 0xFu) == 2u
          : op == D3DTOP_SELECTARG2 ? (ca2 & 0xFu) == 2u
                                    : ((ca1 & 0xFu) == 2u || (ca2 & 0xFu) == 2u);
        if (op == D3DTOP_DISABLE || (!req->texture && needs_tex)) {
            out->texop = GPU_TEXOP_NONE;
            if (!req->texture) {
                g_texop_none_notex++;
                untex_note(op, ca1, ca2);
            } else g_texop_none_disabled++;
        } else if (op == D3DTOP_SELECTARG2) {
            /* Reads arg2 and nothing else -- the case the bail above hid. */
            out->texop = GPU_TEXOP_SELECT_ARG2;
            g_texop_select2++;
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
    out->depth_bias = rs(s, D3DRS_ZBIAS, 0);
    out->stencil_enable = rs(s, 52, 0) != 0;
    out->stencil_fail = rs(s, 53, 1);
    out->stencil_zfail = rs(s, 54, 1);
    out->stencil_pass = rs(s, 55, 1);
    out->stencil_func = rs(s, 56, 8);
    out->stencil_ref = rs(s, 57, 0);
    out->stencil_mask = rs(s, 58, 0xffffffffu);
    out->stencil_write_mask = rs(s, 59, 0xffffffffu);
    out->color_write_mask = rs(s, 168, 0x0fu);

    cull = rs(s, D3DRS_CULLMODE, 3);          /* D3DCULL_CCW */
    out->cull = cull == 1 ? GPU_CULL_NONE
              : cull == 2 ? GPU_CULL_CW
                          : GPU_CULL_CCW;

    out->alpha_test = rs(s, D3DRS_ALPHATESTENABLE, 0) != 0;
    out->alpha_ref = (float)(rs(s, D3DRS_ALPHAREF, 0) & 0xFFu) / 255.0f;

    /* LAST, so the table reports the texture this draw ended up with. Called
       earlier it printed `tex 0` for all 434 draws of a frame whose walls are
       plainly textured -- a column that said "untextured" about everything
       because it ran before the texture was resolved. */
    frame_table_note(req, out, out->vertex_stride,
                     programmable ? -1 : vl.pos_offset,
                     programmable ? -1 : vl.normal_offset, fvf);
    return 1;
}

void d3d8_release_draw(GpuDraw *draw)
{
    if (draw->owns_vertices && draw->vertices)
        gpu_buffer_destroy(draw->vertices);
    draw->vertices = 0;
    draw->owns_vertices = 0;
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

void d3d8_drawcall_report(void)
{
    /*
     * The light dump ALWAYS reports, including when it printed nothing.
     *
     * A dump that never fires -- because the skip was larger than the number
     * of qualifying draws, or because no frame ever passed the draw threshold
     * -- is indistinguishable from a dump that found nothing worth printing.
     * One of those is a measurement and the other is an instrument that never
     * ran, and this line is what tells them apart.
     */
    if (g_ld_want > 0)
        printf("        X2_LIGHT_DUMP: %ld draw(s) qualified (lit, past the "
               "scene gate, in a busy frame); %ld skipped by "
               "X2_LIGHT_DUMP_SKIP; %ld of the %ld asked for were printed%s\n",
               g_ld_qualified, g_ld_skipped, g_ld_done, g_ld_want,
               g_ld_done ? "." :
               " -- so this run's dump says NOTHING about the lighting.");
    light_survey_report();
    fvf_report();
    printf("        light table vs SetLight: %lu enabled-light read(s) "
           "compared, %lu differ from what SetLight last wrote, %lu arrive "
           "BLACK at a draw although SetLight wrote a colour, %lu were never "
           "set at all\n",
           g_lc_checked, g_lc_differ, g_lc_lost, g_lc_neverset);
    /* ALWAYS, including the all-clear: "0 of 290002 draws read outside their
       stream" is a measurement, and a line that only appears when something is
       wrong cannot be told apart from a check that never ran. */
    printf("        vertex range: %lu draw(s) checked, %lu read OUTSIDE their "
           "stream and were refused, %lu could not be checked (no host-"
           "readable indices, or indices past the end of their own buffer)\n",
           g_rng_checked, g_rng_bad, g_rng_unverifiable);
    if (g_rng_bad)
        printf("          worst: needed vertex %u from a stream of %u\n",
               g_rng_worst_need - 1u, g_rng_worst_have);

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
    printf("        texture stage: %lu modulate, %lu select-texture, %lu "
           "select-arg2, %lu add (environment map), %lu other-op-as-modulate, "
           "%lu UNTEXTURED (%lu with no texture bound, %lu with the stage "
           "disabled)\n",
           g_texop_modulate, g_texop_select, g_texop_select2, g_texop_add,
           g_texop_other,
           g_texop_none_notex + g_texop_none_disabled,
           g_texop_none_notex, g_texop_none_disabled);
    d3d8_untextured_report();
}
