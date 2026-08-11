/* See d3d8_state.h. */
#include "d3d8_state.h"

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
     * A count of 47 says the mirror is being written to; it does not say
     * whether the engine asked for fog, or lighting, or a texture factor --
     * each of which is a visible part of the picture this host silently does
     * not produce. Naming them turns "the sky is white" into a list.
     */
    {
        /* D3DRENDERSTATETYPE, from d3d8types.h. */
        static const struct { int id; const char *name; } UNIMPL[] = {
            {  28, "D3DRS_FOGENABLE"      }, {  29, "D3DRS_SPECULARENABLE" },
            {  34, "D3DRS_FOGCOLOR"       }, {  35, "D3DRS_FOGTABLEMODE"   },
            {  36, "D3DRS_FOGSTART"       }, {  37, "D3DRS_FOGEND"         },
            {  38, "D3DRS_FOGDENSITY"     }, {  60, "D3DRS_TEXTUREFACTOR"  },
            { 137, "D3DRS_LIGHTING"       }, { 139, "D3DRS_AMBIENT"        },
            { 140, "D3DRS_FOGVERTEXMODE"  }, { 141, "D3DRS_COLORVERTEX"    },
        };
        int j, any = 0;
        for (j = 0; j < (int)(sizeof UNIMPL / sizeof UNIMPL[0]); j++) {
            if (UNIMPL[j].id >= D3D8_MAX_RENDER_STATES) continue;
            if (!s->render[UNIMPL[j].id].set) continue;
            if (!any++)
                printf("        set, and NOT implemented by this backend -- "
                       "each is missing from the picture:\n");
            printf("          %-22s = %u (0x%08x)\n", UNIMPL[j].name,
                   s->render[UNIMPL[j].id].value,
                   s->render[UNIMPL[j].id].value);
        }
        if (!any && nrender)
            printf("        none of fog, lighting, colour-vertex or the "
                   "texture factor was ever set, so none of them explains a "
                   "wrong colour.\n");
    }
}
