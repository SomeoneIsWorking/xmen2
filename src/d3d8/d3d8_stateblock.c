#include "../config/environment.h"
#include "../native/x2_log.h"
/* See d3d8_stateblock.h. */
#include "d3d8_com.h"
#include "d3d8_stateblock.h"

#include <stdio.h>
#include <stdlib.h>
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
  int used;
  uint32_t gen;
  uint32_t type;
  D3D8State state;
  unsigned long applies, captures;
} Block;

static Block g_sb[SB_MAX];
static uint32_t g_gen = 1;
static unsigned long g_created, g_applied, g_captured, g_deleted, g_refused;
/* What Apply actually DOES to the lighting inputs, over the whole run. */
static unsigned long g_apply_light_changed, g_apply_light_darkened,
    g_apply_material_changed;

/* token = (generation << 8) | (index + 1). Index 0 is never a token, so 0 --
   the value an uninitialised guest DWORD holds -- is always invalid. */
static uint32_t sb_token(unsigned i, uint32_t gen) {
  return (gen << 8) | (uint32_t)(i + 1u);
}

static Block *sb_of(uint32_t token, const char *what) {
  unsigned i = (token & 0xFFu);
  uint32_t gen = token >> 8;
  if (i == 0 || i > SB_MAX) {
    x2_log_error("d3d8: %s was given 0x%08x, which is not a state-block "
                 "token (index %u is outside 1..%d).\n",
                 what, token, i, SB_MAX);
    g_refused++;
    return NULL;
  }
  i--;
  if (!g_sb[i].used) {
    x2_log_error("d3d8: %s was given 0x%08x, whose slot %u holds no "
                 "live block -- it was deleted, or never created.\n",
                 what, token, i);
    g_refused++;
    return NULL;
  }
  if (g_sb[i].gen != gen) {
    x2_log_error("d3d8: %s was given 0x%08x, a STALE token: slot %u now "
                 "holds generation %u, not %u. The block it names was "
                 "deleted and the slot reused.\n",
                 what, token, i, g_sb[i].gen, gen);
    g_refused++;
    return NULL;
  }
  return &g_sb[i];
}

int d3d8_sb_create(uint32_t type, const D3D8State *now, uint32_t *token_out) {
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
    x2_log_error("d3d8: CreateStateBlock(type=%u) -- only D3DSBT_ALL "
                 "(1) is implemented.\n"
                 "  %s captures a documented SUBSET of the state, and "
                 "capturing everything instead would\n"
                 "  replay state the engine meant to keep. See "
                 "src/d3d8/d3d8_stateblock.c.\n",
                 type,
                 type == D3DSBT_PIXELSTATE    ? "D3DSBT_PIXELSTATE"
                 : type == D3DSBT_VERTEXSTATE ? "D3DSBT_VERTEXSTATE"
                                              : "that type");
    g_refused++;
    return 0;
  }
  for (i = 0; i < SB_MAX; i++)
    if (!g_sb[i].used)
      break;
  if (i == SB_MAX) {
    x2_log_error("d3d8: CreateStateBlock -- all %d state-block slots "
                 "are live. Raise SB_MAX in src/d3d8/d3d8_stateblock.c; "
                 "this is a fixed table, not a leak report.\n",
                 SB_MAX);
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

int d3d8_sb_capture(uint32_t token, const D3D8State *now) {
  Block *b = sb_of(token, "CaptureStateBlock");
  if (!b)
    return 0;
  b->state = *now;
  b->captures++;
  g_captured++;
  return 1;
}

int d3d8_sb_apply(uint32_t token, D3D8State *dst) {
  Block *b = sb_of(token, "ApplyStateBlock");
  if (!b)
    return 0;
  /*
   * X2_SB_DUMP=<n> -- what Apply CHANGES, for the first n applies.
   *
   * The level renders black with a material of 0,0,0 at every lit draw,
   * while SetMaterial is measured receiving 1,1,1,1. Something between the
   * two overwrites the mirror, and this is the only thing that overwrites
   * ALL of it at once. Printing the before and after says whether it is --
   * and prints "unchanged" too, because Apply being innocent is just as
   * useful an answer and would otherwise look like no output.
   */
  {
    static long want = -2, done;
    if (want == -2) {
      const char *e = x2_config_override_get(kX2ConfigStateBlockDump);
      want = (e && *e) ? atol(e) : -1;
    }
    if (want > 0 && done < want) {
      done++;
      if (memcmp(dst->light, b->state.light, sizeof dst->light) != 0 ||
          memcmp(dst->light_on, b->state.light_on, sizeof dst->light_on) != 0)
        x2_log_error("d3d8 sb apply %ld/%ld: THE LIGHT TABLE "
                     "CHANGES. light[7] diffuse %.3f %.3f %.3f -> %.3f "
                     "%.3f %.3f, light_on[7] %u -> %u\n",
                     done, want, dst->light[7][1], dst->light[7][2],
                     dst->light[7][3], b->state.light[7][1],
                     b->state.light[7][2], b->state.light[7][3],
                     dst->light_on[7], b->state.light_on[7]);
      else
        x2_log_error("d3d8 sb apply %ld/%ld: light table unchanged "
                     "(light[7] diffuse %.3f %.3f %.3f)\n",
                     done, want, dst->light[7][1], dst->light[7][2],
                     dst->light[7][3]);
      if (memcmp(dst->material, b->state.material, sizeof dst->material) == 0)
        x2_log_error("d3d8 sb apply %ld/%ld: material UNCHANGED "
                     "(%.3f %.3f %.3f)\n",
                     done, want, dst->material[0], dst->material[1],
                     dst->material[2]);
      else
        x2_log_error("d3d8 sb apply %ld/%ld: material %.3f %.3f "
                     "%.3f  ->  %.3f %.3f %.3f   (block captured when "
                     "material_set=%d)\n",
                     done, want, dst->material[0], dst->material[1],
                     dst->material[2], b->state.material[0],
                     b->state.material[1], b->state.material[2],
                     b->state.material_set);
    }
  }
  /*
   * OVER THE WHOLE RUN, not the first n.
   *
   * The dump above answers "what did apply number three do", and the first
   * applies of a run are the MENU -- the scene that is known correct. The
   * question is whether Apply is what blacks out a LEVEL's lights, and that
   * needs every apply counted, with the denominator, so a zero is a
   * measurement rather than a line nobody printed.
   *
   * "Darkens" is the discriminating case: an apply that replaces a lit light
   * table with a black one is the shape of the defect; one that does the
   * reverse, or leaves it alone, is not.
   */
  {
    int changed =
        memcmp(dst->light, b->state.light, sizeof dst->light) != 0 ||
        memcmp(dst->light_on, b->state.light_on, sizeof dst->light_on) != 0;
    if (changed) {
      unsigned i;
      int lit_before = 0, lit_after = 0;
      for (i = 0; i < D3D8_MAX_LIGHTS; i++) {
        int on_b = dst->light_on[i], on_a = b->state.light_on[i];
        int nz_b = dst->light[i][1] != 0.0f || dst->light[i][2] != 0.0f ||
                   dst->light[i][3] != 0.0f;
        int nz_a = b->state.light[i][1] != 0.0f ||
                   b->state.light[i][2] != 0.0f || b->state.light[i][3] != 0.0f;
        lit_before += on_b && nz_b;
        lit_after += on_a && nz_a;
      }
      g_apply_light_changed++;
      if (lit_after < lit_before)
        g_apply_light_darkened++;
    }
    if (memcmp(dst->material, b->state.material, sizeof dst->material) != 0)
      g_apply_material_changed++;
  }
  *dst = b->state;
  b->applies++;
  g_applied++;
  return 1;
}

int d3d8_sb_delete(uint32_t token) {
  Block *b = sb_of(token, "DeleteStateBlock");
  if (!b)
    return 0;
  b->used = 0; /* gen is NOT reset: that is what makes the
                  old token detectable once the slot is
                  reused. */
  g_deleted++;
  return 1;
}

void d3d8_sb_report(void) {
  unsigned i, live = 0;
  for (i = 0; i < SB_MAX; i++)
    if (g_sb[i].used)
      live++;
  if (!g_created && !g_refused) {
    x2_log_info("  d3d8: no state block was ever created.\n");
    return;
  }
  x2_log_info(
      "  d3d8: %lu state block(s) created, %lu applied, %lu re-captured, "
      "%lu deleted, %u still live\n",
      g_created, g_applied, g_captured, g_deleted, live);
  if (g_created && !g_applied)
    x2_log_info("        NONE of them was ever applied -- the engine captured "
                "state it never restored, so whatever those blocks were "
                "protecting is being drawn with whatever came after.\n");
  if (g_refused)
    x2_log_info("        %lu call(s) were REFUSED (unsupported type, exhausted "
                "table, or a token naming no live block); each said which.\n",
                g_refused);
  /*
   * Printed at ZERO as well, with the denominator. Apply restoring the whole
   * state is what D3DSBT_ALL MEANS, so this is not a defect report -- it is
   * the number that says whether Apply is what leaves a level's lights black
   * by the time a draw reads them, or whether it never touches them at all.
   */
  x2_log_info(
      "        of those %lu applies, %lu changed the light table (%lu of "
      "them left FEWER lights both enabled and non-black than were there "
      "before) and %lu changed the material.\n",
      g_applied, g_apply_light_changed, g_apply_light_darkened,
      g_apply_material_changed);
}
