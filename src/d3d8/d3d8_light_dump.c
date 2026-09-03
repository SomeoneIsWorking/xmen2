/*
 * X2_LIGHT_DUMP: the lighting INPUTS of the first n lit draws, printed.
 *
 * Split from d3d8_lighting.c, which COMPUTES the lighting. This only
 * observes it, and owns the three values the fill path hands it: the
 * camera-space position of the draw, which D3D index each packed light came
 * from, and the raw ambient D3DCOLOR before conversion.
 */
#include "d3d8_light_dump.h"

#include "d3d8_lighting.h"
#include "d3d8_render_states.h"
#include "d3d8_state.h"
#include "gpu_device.h"
#include "gpu_draw.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
/* Where the draw sits in CAMERA space, for the dump. D3D8 defines light
   positions in WORLD space and this stage lights there, so a draw whose world
   position is thousands of units from every light while its camera position is
   a few hundred says the two are not in the same space -- which no colour in
   the dump could ever say. */
static float g_ld_viewpos[3];
/* Which D3D light INDEX each packed light came from. The dump prints "light 0"
   meaning the first ENABLED one, which is not the index the engine set -- and
   without the index a black light cannot be traced back to the SetLight that
   made it. */
static int g_light_src[8];
/* The raw D3DCOLOR the engine set, kept so the dump can show what was READ as
   well as what it became -- a zero after conversion and a zero in the register
   are different faults. */
static uint32_t g_last_ambient_raw;

void d3d8_light_note_ambient(uint32_t raw) { g_last_ambient_raw = raw; }
void d3d8_light_note_source(int slot, int d3d_index) {
  if (slot >= 0 && slot < 8)
    g_light_src[slot] = d3d_index;
}
void d3d8_light_note_viewpos(float x, float y, float z) {
  g_ld_viewpos[0] = x;
  g_ld_viewpos[1] = y;
  g_ld_viewpos[2] = z;
}

int d3d8_light_source_index(int slot) {
  return slot >= 0 && slot < 8 ? g_light_src[slot] : -1;
}

static long g_ld_want = -2, g_ld_done, g_ld_skip = -1, g_ld_skipped,
            g_ld_qualified;

/*
 * How many draws were asked for, read once.
 *
 * The REPORT asks too, not only the dump: a run with no lit draw at all never
 * enters the dump, and if the request were read there alone the report would
 * fall silent -- which is precisely the "asked for a dump and got nothing"
 * case it exists to name.
 */
static long dump_requested(void) {
  if (g_ld_want == -2) {
    const char *e = getenv("X2_LIGHT_DUMP");
    g_ld_want = (e && *e) ? atol(e) : -1;
  }
  return g_ld_want;
}

void d3d8_light_dump(const GpuDraw *d) {
  int i;

  if (dump_requested() <= 0)
    return;
  if (!d->lighting)
    return;
  /*
   * The SCENE gate first, when one was asked for. A draw count separates a
   * movie from "some scene" and nothing finer -- it let a menu frame be
   * dumped and three readings had to be retracted. X2_SHOT_AFTER_FILE names
   * the scene by the file the game opens, so the two instruments aim at the
   * same frame and a dump can be held until the level is on screen. */
  {
    extern int k32_file_gate_open(void);
    if (!k32_file_gate_open())
      return;
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
    if ((long)gpu_frame_draws_so_far() < minimum)
      return;
    /* ONCE. This was gated on "nothing dumped yet", which is true for
       every skipped draw as well, so with X2_LIGHT_DUMP_SKIP it printed
       the same line thousands of times. */
    if (!told++)
      fprintf(stderr,
              "d3d8: X2_LIGHT_DUMP -- only frames that have "
              "already submitted %ld draw(s) are dumped (set "
              "X2_LIGHT_DUMP_MIN to change). A menu frame submits far "
              "fewer, so this is NOT the menu.\n",
              minimum);
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
  if (g_ld_done >= g_ld_want)
    return;
  {
    if (g_ld_skip < 0) {
      const char *e = getenv("X2_LIGHT_DUMP_SKIP");
      g_ld_skip = (e && *e) ? atol(e) : 0;
    }
    if (g_ld_skipped < g_ld_skip) {
      if (++g_ld_skipped == g_ld_skip)
        fprintf(stderr,
                "d3d8: X2_LIGHT_DUMP_SKIP -- %ld qualifying "
                "draw(s) were skipped; what follows is LATER in the "
                "level, not its first lit frames.\n",
                g_ld_skip);
      return;
    }
  }
  g_ld_done++;
  fprintf(
      stderr,
      "d3d8 light dump %ld/%ld at presented frame %lu: %d light(s) enabled, "
      "ambient %.3f %.3f %.3f "
      "(D3DRS_AMBIENT raw 0x%08x), colorvertex %d, has_normal %d\n"
      "    material diffuse %.3f %.3f %.3f  ambient %.3f %.3f %.3f  "
      "emissive %.3f %.3f %.3f\n",
      g_ld_done, g_ld_want, gpu_frames_presented(), d->nlights,
      d->global_ambient[0], d->global_ambient[1], d->global_ambient[2],
      g_last_ambient_raw, d->color_vertex, d->normal_offset >= 0,
      d->mat_diffuse[0], d->mat_diffuse[1], d->mat_diffuse[2],
      d->mat_ambient[0], d->mat_ambient[1], d->mat_ambient[2],
      d->mat_emissive[0], d->mat_emissive[1], d->mat_emissive[2]);
  fprintf(
      stderr,
      "    world row0 %.3f %.3f %.3f %.3f   row3(translation) %.1f %.1f %.1f\n"
      "    the same origin in CAMERA space (world*view): %.1f %.1f %.1f\n"
      "    draw: %u primitive(s), stride %u, texture %u, %s\n",
      d->world[0], d->world[1], d->world[2], d->world[3], d->world[12],
      d->world[13], d->world[14], g_ld_viewpos[0], g_ld_viewpos[1],
      g_ld_viewpos[2], d->prim_count, d->vertex_stride, d->texture,
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
            i, i < 8 ? g_light_src[i] : -1, L->type, L->diffuse[0],
            L->diffuse[1], L->diffuse[2], L->ambient[0], L->ambient[1],
            L->ambient[2], L->position[0], L->position[1], L->position[2],
            L->direction[0], L->direction[1], L->direction[2], L->range,
            L->atten[0], L->atten[1], L->atten[2]);
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
    if (L->type == 1) { /* D3DLIGHT_POINT */
      dw[0] = L->position[0] - d->world[12];
      dw[1] = L->position[1] - d->world[13];
      dw[2] = L->position[2] - d->world[14];
      dv[0] = L->position[0] - g_ld_viewpos[0];
      dv[1] = L->position[1] - g_ld_viewpos[1];
      dv[2] = L->position[2] - g_ld_viewpos[2];
      distw = sqrtf(dw[0] * dw[0] + dw[1] * dw[1] + dw[2] * dw[2]);
      distv = sqrtf(dv[0] * dv[0] + dv[1] * dv[1] + dv[2] * dv[2]);
      den = L->atten[0] + L->atten[1] * distw + L->atten[2] * distw * distw;
      attw = den > 0.0f ? 1.0f / den : 1.0f;
      den = L->atten[0] + L->atten[1] * distv + L->atten[2] * distv * distv;
      attv = den > 0.0f ? 1.0f / den : 1.0f;
      fprintf(stderr,
              "            from this draw's WORLD origin: %.0f units, "
              "attenuation %.4f%s\n"
              "            from its CAMERA origin:        %.0f units, "
              "attenuation %.4f%s\n",
              distw, attw, attw < 0.05f ? "   <- effectively BLACK" : "", distv,
              attv, attv < 0.05f ? "   <- effectively BLACK" : "");
    }
  }
}

/*
 * What the two light diagnostics saw this run.
 *
 * Reported from here because the tallies live here: the run report used to
 * print them from d3d8_drawcall.c, which could only do it by reaching into
 * another file's counters.
 */
void d3d8_light_dump_report(void) {
  /*
   * The light dump ALWAYS reports, including when it printed nothing.
   *
   * A dump that never fires -- because the skip was larger than the number
   * of qualifying draws, or because no frame ever passed the draw threshold
   * -- is indistinguishable from a dump that found nothing worth printing.
   * One of those is a measurement and the other is an instrument that never
   * ran, and this line is what tells them apart.
   */
  if (dump_requested() > 0)
    printf("        X2_LIGHT_DUMP: %ld draw(s) qualified (lit, past the "
           "scene gate, in a busy frame); %ld skipped by "
           "X2_LIGHT_DUMP_SKIP; %ld of the %ld asked for were printed%s\n",
           g_ld_qualified, g_ld_skipped, g_ld_done, g_ld_want,
           g_ld_done
               ? "."
               : " -- so this run's dump says NOTHING about the lighting.");
}
