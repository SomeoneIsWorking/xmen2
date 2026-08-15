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
