/* See d3d8_state.h. */
#include "d3d8_state.h"
#include "d3d8_drawcall.h"

#include <stdio.h>
#include <string.h>

void d3d8_state_reset(D3D8State *s)
{
    memset(s, 0, sizeof *s);
    s->viewport_maxz = 1.0f;
}

static int range(uint32_t v, uint32_t n, const char *what)
{
    if (v < n) return 1;
    fprintf(stderr, "d3d8: %s %u is outside the %u this device keeps. "
                    "Refusing rather than wrapping -- a wrapped index would "
                    "overwrite an unrelated state and draw wrong later.\n",
            what, v, n);
    return 0;
}

int d3d8_state_set_render(D3D8State *s, uint32_t which, uint32_t value)
{
    if (!range(which, D3D8_MAX_RENDER_STATES, "render state")) return 0;
    s->render[which].value = value;
    s->render[which].set = 1;
    return 1;
}

int d3d8_state_get_render(const D3D8State *s, uint32_t which, uint32_t *out)
{
    if (!range(which, D3D8_MAX_RENDER_STATES, "render state")) return 0;
    *out = s->render[which].value;
    return 1;
}

int d3d8_state_set_stage(D3D8State *s, uint32_t stage, uint32_t which,
                         uint32_t value)
{
    if (!range(stage, D3D8_MAX_STAGES, "texture stage")) return 0;
    if (!range(which, D3D8_MAX_STAGE_STATES, "texture stage state")) return 0;
    s->stage[stage][which].value = value;
    s->stage[stage][which].set = 1;
    return 1;
}

int d3d8_state_get_stage(const D3D8State *s, uint32_t stage, uint32_t which,
                         uint32_t *out)
{
    if (!range(stage, D3D8_MAX_STAGES, "texture stage")) return 0;
    if (!range(which, D3D8_MAX_STAGE_STATES, "texture stage state")) return 0;
    *out = s->stage[stage][which].value;
    return 1;
}

/*
 * D3DRENDERSTATETYPE by number, for the report.
 *
 * Names only the ones this project has had reason to look up; anything else
 * prints as its number rather than being left out, because a state missing
 * from a list of what is missing is the failure this report exists to avoid.
 */
const char *d3d8_render_state_name(int id)
{
    static const struct { int id; const char *name; } NAMES[] = {
        {   7, "D3DRS_ZENABLE"         }, {   8, "D3DRS_FILLMODE"        },
        {   9, "D3DRS_SHADEMODE"       }, {  10, "D3DRS_LINEPATTERN"     },
        {  14, "D3DRS_ZWRITEENABLE"    }, {  15, "D3DRS_ALPHATESTENABLE" },
        {  16, "D3DRS_LASTPIXEL"       }, {  19, "D3DRS_SRCBLEND"        },
        {  20, "D3DRS_DESTBLEND"       }, {  22, "D3DRS_CULLMODE"        },
        {  23, "D3DRS_ZFUNC"           }, {  24, "D3DRS_ALPHAREF"        },
        {  25, "D3DRS_ALPHAFUNC"       }, {  26, "D3DRS_DITHERENABLE"    },
        {  27, "D3DRS_ALPHABLENDENABLE"}, {  28, "D3DRS_FOGENABLE"       },
        {  29, "D3DRS_SPECULARENABLE"  }, {  34, "D3DRS_FOGCOLOR"        },
        {  35, "D3DRS_FOGTABLEMODE"    }, {  36, "D3DRS_FOGSTART"        },
        {  37, "D3DRS_FOGEND"          }, {  38, "D3DRS_FOGDENSITY"      },
        {  47, "D3DRS_ZBIAS"           }, {  48, "D3DRS_RANGEFOGENABLE"  },
        {  52, "D3DRS_STENCILENABLE"   }, {  53, "D3DRS_STENCILFAIL"     },
        {  54, "D3DRS_STENCILZFAIL"    }, {  55, "D3DRS_STENCILPASS"     },
        {  56, "D3DRS_STENCILFUNC"     }, {  57, "D3DRS_STENCILREF"      },
        {  58, "D3DRS_STENCILMASK"     }, {  59, "D3DRS_STENCILWRITEMASK"},
        {  60, "D3DRS_TEXTUREFACTOR"   }, { 128, "D3DRS_WRAP0"           },
        { 136, "D3DRS_CLIPPING"        }, { 137, "D3DRS_LIGHTING"        },
        { 139, "D3DRS_AMBIENT"         }, { 140, "D3DRS_FOGVERTEXMODE"   },
        { 141, "D3DRS_COLORVERTEX"     }, { 142, "D3DRS_LOCALVIEWER"     },
        { 143, "D3DRS_NORMALIZENORMALS" },
        { 145, "D3DRS_DIFFUSEMATERIALSOURCE"  },
        { 146, "D3DRS_SPECULARMATERIALSOURCE" },
        { 147, "D3DRS_AMBIENTMATERIALSOURCE"  },
        { 148, "D3DRS_EMISSIVEMATERIALSOURCE" },
        { 151, "D3DRS_VERTEXBLEND"     }, { 152, "D3DRS_CLIPPLANEENABLE" },
        { 153, "D3DRS_SOFTWAREVERTEXPROCESSING" },
        { 154, "D3DRS_POINTSIZE"       }, { 155, "D3DRS_POINTSIZE_MIN"   },
        { 156, "D3DRS_POINTSPRITEENABLE" },
        { 157, "D3DRS_POINTSCALEENABLE"  },
        { 161, "D3DRS_MULTISAMPLEANTIALIAS" },
        { 162, "D3DRS_MULTISAMPLEMASK" }, { 163, "D3DRS_PATCHEDGESTYLE"  },
        { 165, "D3DRS_DEBUGMONITORTOKEN" },
        { 166, "D3DRS_POINTSIZE_MAX"   },
        { 167, "D3DRS_INDEXEDVERTEXBLENDENABLE" },
        { 168, "D3DRS_COLORWRITEENABLE" }, { 170, "D3DRS_TWEENFACTOR"    },
        { 171, "D3DRS_BLENDOP"         }, { 172, "D3DRS_POSITIONORDER"   },
        { 173, "D3DRS_NORMALORDER"     },
    };
    static char other[32];
    unsigned i;
    for (i = 0; i < sizeof NAMES / sizeof NAMES[0]; i++)
        if (NAMES[i].id == id) return NAMES[i].name;
    snprintf(other, sizeof other, "D3DRS #%d", id);
    return other;
}

void d3d8_state_report(const D3D8State *s)
{
    int i, k, nrender = 0, nstage = 0, ntransform = 0, nlight = 0;

    for (i = 0; i < D3D8_MAX_RENDER_STATES; i++) nrender += s->render[i].set != 0;
    for (i = 0; i < D3D8_MAX_STAGES; i++)
        for (k = 0; k < D3D8_MAX_STAGE_STATES; k++)
            nstage += s->stage[i][k].set != 0;
    for (i = 0; i < D3D8_MAX_TRANSFORMS; i++) ntransform += s->transform_set[i] != 0;
    for (i = 0; i < D3D8_MAX_LIGHTS; i++) nlight += s->light_set[i] != 0;

    printf("  d3d8 state the engine set: %d render state(s), %d texture stage "
           "state(s),\n"
           "        %d transform(s), %d light(s), material %s, viewport %s\n",
           nrender, nstage, ntransform, nlight,
           s->material_set ? "set" : "never set",
           s->viewport_set ? "set" : "never set");
    if (!nrender)
        printf("        NOTHING was set. Either the engine never reached its "
               "state setup, or the setters are not wired to this mirror.\n");
    /*
     * The states this backend does NOT implement, with their last value.
     *
     * ENUMERATED, not intersected with a list of the ones somebody thought of.
     * The first version of this report walked a hand-written table of thirteen
     * render states, so it could only ever name a state that had already been
     * considered -- and it read as a complete list of what was missing. Every
     * state the engine sets is now asked about, and the ones this backend does
     * not read are printed whether or not anyone has named them; an unnamed one
     * prints its number, which is enough to look up and far better than the
     * silence it used to get.
     */
    {
        int j, any = 0, unread = 0;
        for (j = 0; j < D3D8_MAX_RENDER_STATES; j++) {
            if (!s->render[j].set) continue;
            if (d3d8_drawcall_reads_state((uint32_t)j)) continue;
            unread++;
            if (!any++)
                printf("        set, and NOT read by this backend -- each is "
                       "missing from the picture:\n");
            printf("          %-24s = %u (0x%08x)\n", d3d8_render_state_name(j),
                   s->render[j].value, s->render[j].value);
        }
        printf("        %d of the %d render state(s) the engine set are read "
               "by the draw path; %d are not.\n",
               nrender - unread, nrender, unread);
    }
    /*
     * The transforms, the same way.
     *
     * D3D8 keeps 256 world matrices, and the fixed-function pipeline blends
     * between the first few when D3DRS_VERTEXBLEND is on -- which is how an
     * engine of this age skins a character. This backend reads WORLD, VIEW and
     * PROJECTION and nothing else, so a set WORLDMATRIX(1..) means every vertex
     * of that mesh is being transformed by matrix 0 alone. The count of
     * transforms alone could never say that.
     */
    {
        int j, unread = 0;
        for (j = 0; j < D3D8_MAX_TRANSFORMS; j++) {
            if (!s->transform_set[j]) continue;
            if (j == 2 || j == 3 || j == 256) continue;   /* VIEW/PROJ/WORLD */
            if (!unread++)
                printf("        transform(s) the engine set that the draw path "
                       "does NOT read:\n");
            if (j > 256)
                printf("          D3DTS_WORLDMATRIX(%d)\n", j - 256);
            else
                printf("          D3DTS #%d\n", j);
        }
        printf("        %d of the %d transform(s) the engine set are read "
               "(WORLD, VIEW, PROJECTION); %d are not.\n",
               ntransform - unread, ntransform, unread);
    }
}
