/*
 * X2_LIGHT_SURVEY: which draws of a frame are lit, and by what.
 *
 * Split from d3d8_lighting.c for the same reason as the dump: it observes
 * the lighting rather than computing it, and it keeps a frame's worth of
 * signature state of its own to do so.
 */
#include "d3d8_light_survey.h"

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
static int g_sv_on = -1;
static unsigned long g_sv_frames, g_sv_seen, g_sv_unlit, g_sv_lit, g_sv_black,
    g_sv_nolights, g_sv_black_noatten, g_sv_vertexcol;
static struct {
  unsigned stride, texgen, textured, nlights, black_only_atten;
  unsigned long count;
} g_sv_sig[SURVEY_SIGS];
static int g_sv_nsig;
static unsigned long g_sv_sig_lost;
static unsigned long g_sv_last_frame = ~0UL;
static int g_sv_started;
static unsigned long g_sv_start_frame, g_sv_ungated;

static float survey_bound(const GpuDraw *d, int ignore_atten) {
  float acc[3], dm[3], am[3], em[3];
  int c, i;
  int diffuse_vertex =
      d->color_vertex && ((d->diffuse_source == 1u && d->color_offset >= 0) ||
                          (d->diffuse_source == 2u && d->specular_offset >= 0));
  int ambient_vertex =
      d->color_vertex && ((d->ambient_source == 1u && d->color_offset >= 0) ||
                          (d->ambient_source == 2u && d->specular_offset >= 0));
  int emissive_vertex = d->color_vertex &&
                        ((d->emissive_source == 1u && d->color_offset >= 0) ||
                         (d->emissive_source == 2u && d->specular_offset >= 0));

  for (c = 0; c < 3; c++) {
    dm[c] = diffuse_vertex ? 1.0f : d->mat_diffuse[c];
    am[c] = ambient_vertex ? 1.0f : d->mat_ambient[c];
    em[c] = emissive_vertex ? 1.0f : d->mat_emissive[c];
    acc[c] = em[c] + am[c] * d->global_ambient[c];
  }
  for (i = 0; i < d->nlights; i++) {
    const GpuLight *L = &d->light[i];
    float atten = 1.0f;
    if (L->type != 3 && !ignore_atten) { /* not DIRECTIONAL */
      float dx = L->position[0] - d->world[12];
      float dy = L->position[1] - d->world[13];
      float dz = L->position[2] - d->world[14];
      float dist = sqrtf(dx * dx + dy * dy + dz * dz), den;
      if (dist > L->range)
        continue; /* the shader drops it too */
      den = L->atten[0] + L->atten[1] * dist + L->atten[2] * dist * dist;
      atten = den > 0.0f ? 1.0f / den : 1.0f;
    }
    for (c = 0; c < 3; c++)
      acc[c] += (am[c] * L->ambient[c] + dm[c] * L->diffuse[c]) * atten;
  }
  for (c = 0; c < 3; c++)
    if (acc[c] > 1.0f)
      acc[c] = 1.0f;
  return 0.299f * acc[0] + 0.587f * acc[1] + 0.114f * acc[2];
}

void d3d8_light_survey(const GpuDraw *d) {
  float bound, bound_noatten;
  int black, only_atten, i;

  if (g_sv_on < 0) {
    const char *e = getenv("X2_LIGHT_SURVEY");
    g_sv_on = (e && *e) ? atoi(e) : 0;
  }
  if (!g_sv_on)
    return;
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
    if (!k32_file_gate_open()) {
      g_sv_ungated++;
      return;
    }
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
      fprintf(stderr,
              "[SURVEY] gameplay reached at presented frame "
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
        if (every < 1)
          every = 1;
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
  if (!d->lighting) {
    g_sv_unlit++;
    return;
  }
  g_sv_lit++;
  if (d->color_vertex && ((d->diffuse_source == 1u && d->color_offset >= 0) ||
                          (d->diffuse_source == 2u && d->specular_offset >= 0)))
    g_sv_vertexcol++;
  if (!d->nlights)
    g_sv_nolights++;

  bound = survey_bound(d, 0);
  bound_noatten = survey_bound(d, 1);
  black = bound < 0.02f;
  only_atten = black && bound_noatten >= 0.02f;
  if (!black)
    return;
  g_sv_black++;
  if (only_atten)
    g_sv_black_noatten++;

  for (i = 0; i < g_sv_nsig; i++)
    if (g_sv_sig[i].stride == d->vertex_stride &&
        g_sv_sig[i].texgen == (unsigned)d->texgen &&
        g_sv_sig[i].textured == (d->texture != 0) &&
        g_sv_sig[i].nlights == (unsigned)d->nlights &&
        g_sv_sig[i].black_only_atten == (unsigned)only_atten) {
      g_sv_sig[i].count++;
      return;
    }
  if (g_sv_nsig == SURVEY_SIGS) {
    g_sv_sig_lost++;
    return;
  }
  g_sv_sig[g_sv_nsig].stride = d->vertex_stride;
  g_sv_sig[g_sv_nsig].texgen = (unsigned)d->texgen;
  g_sv_sig[g_sv_nsig].textured = (d->texture != 0);
  g_sv_sig[g_sv_nsig].nlights = (unsigned)d->nlights;
  g_sv_sig[g_sv_nsig].black_only_atten = (unsigned)only_atten;
  g_sv_sig[g_sv_nsig].count = 1;
  g_sv_nsig++;
}

void d3d8_light_survey_report(void) {
  int i;
  if (g_sv_on <= 0)
    return;
  printf("        X2_LIGHT_SURVEY: %lu draw(s) over %lu gameplay frame(s) "
         "-- EVERY draw submitted from presented frame %lu on; %lu unlit, "
         "%lu lit\n",
         g_sv_seen, g_sv_frames, g_sv_start_frame, g_sv_unlit, g_sv_lit);
  if (!g_sv_seen) {
    printf("          NO DRAW WAS EVER SURVEYED -- gameplay was never "
           "reached (%lu draw(s) went by before the level-open gate or "
           "before any frame submitted enough draws). This says NOTHING "
           "about the lighting.\n",
           g_sv_ungated);
    return;
  }
  printf("          %lu draw(s) before gameplay started are not in this "
         "count\n",
         g_sv_ungated);
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
