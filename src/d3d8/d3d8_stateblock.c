/* See d3d8_stateblock.h. */
#include "d3d8_stateblock.h"
#include "d3d8_com.h"

#include <stdio.h>
#include <string.h>

/*
 * A fixed table, and tokens that carry a generation.
 *
 * The same shape src/gpu/gpu_draw.c uses for its resources, and for the same
 * reason: the guest holds the token, so a stale one has to be CAUGHT. An index
 * on its own cannot be -- a deleted block's slot is reused and the old token
 * silently addresses the new block. With a generation in the high bits, a
 * token from a deleted block matches nothing and says so.
 *
 * 32 blocks: a title that uses these at all uses a handful (one per material
 * or pass), and the count is reported, so a build that needs more says so
 * rather than failing at draw time.
 */
#define SB_MAX 32

typedef struct {
    int       used;
    uint32_t  gen;
    uint32_t  type;
    D3D8State state;
    unsigned long applies, captures;
} Block;

static Block g_sb[SB_MAX];
static uint32_t g_gen = 1;
static unsigned long g_created, g_applied, g_captured, g_deleted, g_refused;

/* token = (generation << 8) | (index + 1). Index 0 is never a token, so 0 --
   the value an uninitialised guest DWORD holds -- is always invalid. */
static uint32_t sb_token(unsigned i, uint32_t gen)
{
    return (gen << 8) | (uint32_t)(i + 1u);
}

static Block *sb_of(uint32_t token, const char *what)
{
    unsigned i = (token & 0xFFu);
    uint32_t gen = token >> 8;
    if (i == 0 || i > SB_MAX) {
        fprintf(stderr, "d3d8: %s was given 0x%08x, which is not a state-block "
                        "token (index %u is outside 1..%d).\n",
                what, token, i, SB_MAX);
        g_refused++;
        return NULL;
    }
    i--;
    if (!g_sb[i].used) {
        fprintf(stderr, "d3d8: %s was given 0x%08x, whose slot %u holds no "
                        "live block -- it was deleted, or never created.\n",
                what, token, i);
        g_refused++;
        return NULL;
    }
    if (g_sb[i].gen != gen) {
        fprintf(stderr, "d3d8: %s was given 0x%08x, a STALE token: slot %u now "
                        "holds generation %u, not %u. The block it names was "
                        "deleted and the slot reused.\n",
                what, token, i, g_sb[i].gen, gen);
        g_refused++;
        return NULL;
    }
    return &g_sb[i];
}

int d3d8_sb_create(uint32_t type, const D3D8State *now, uint32_t *token_out)
{
    unsigned i;

    if (type != D3DSBT_ALL) {
        /*
         * Refused, not approximated.
         *
         * D3DSBT_PIXELSTATE and D3DSBT_VERTEXSTATE capture documented SUBSETS
         * of the render, texture-stage and transform state. Capturing
         * everything instead would apply state the engine deliberately kept
         * out of the block -- so the wrong picture would appear at some later
         * draw with nothing linking it back to here. When a title asks for
         * one of these, the answer is the real subset list, not this.
         */
        fprintf(stderr, "d3d8: CreateStateBlock(type=%u) -- only D3DSBT_ALL "
                        "(1) is implemented.\n"
                        "  %s captures a documented SUBSET of the state, and "
                        "capturing everything instead would\n"
                        "  replay state the engine meant to keep. See "
                        "src/d3d8/d3d8_stateblock.c.\n",
                type,
                type == D3DSBT_PIXELSTATE ? "D3DSBT_PIXELSTATE"
                : type == D3DSBT_VERTEXSTATE ? "D3DSBT_VERTEXSTATE"
                : "that type");
        g_refused++;
        return 0;
    }
    for (i = 0; i < SB_MAX; i++) if (!g_sb[i].used) break;
    if (i == SB_MAX) {
        fprintf(stderr, "d3d8: CreateStateBlock -- all %d state-block slots "
                        "are live. Raise SB_MAX in src/d3d8/d3d8_stateblock.c; "
                        "this is a fixed table, not a leak report.\n", SB_MAX);
        g_refused++;
        return 0;
    }
    g_sb[i].used = 1;
    g_sb[i].gen = g_gen++;
    g_sb[i].type = type;
    g_sb[i].state = *now;
    g_sb[i].applies = g_sb[i].captures = 0;
    *token_out = sb_token(i, g_sb[i].gen);
    g_created++;
    return 1;
}

int d3d8_sb_capture(uint32_t token, const D3D8State *now)
{
    Block *b = sb_of(token, "CaptureStateBlock");
    if (!b) return 0;
    b->state = *now;
    b->captures++;
    g_captured++;
    return 1;
}

int d3d8_sb_apply(uint32_t token, D3D8State *dst)
{
    Block *b = sb_of(token, "ApplyStateBlock");
    if (!b) return 0;
    *dst = b->state;
    b->applies++;
    g_applied++;
    return 1;
}

int d3d8_sb_delete(uint32_t token)
{
    Block *b = sb_of(token, "DeleteStateBlock");
    if (!b) return 0;
    b->used = 0;                 /* gen is NOT reset: that is what makes the
                                    old token detectable once the slot is
                                    reused. */
    g_deleted++;
    return 1;
}

void d3d8_sb_report(void)
{
    unsigned i, live = 0;
    for (i = 0; i < SB_MAX; i++) if (g_sb[i].used) live++;
    if (!g_created && !g_refused) {
        printf("  d3d8: no state block was ever created.\n");
        return;
    }
    printf("  d3d8: %lu state block(s) created, %lu applied, %lu re-captured, "
           "%lu deleted, %u still live\n",
           g_created, g_applied, g_captured, g_deleted, live);
    if (g_created && !g_applied)
        printf("        NONE of them was ever applied -- the engine captured "
               "state it never restored, so whatever those blocks were "
               "protecting is being drawn with whatever came after.\n");
    if (g_refused)
        printf("        %lu call(s) were REFUSED (unsupported type, exhausted "
               "table, or a token naming no live block); each said which.\n",
               g_refused);
}
