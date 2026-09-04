#include "../native/x2_log.h"
/*
 * Fixed-function lighting: D3D8's per-vertex lighting model, and the evidence
 * for what the engine asked of it.
 *
 * D3D8 computes this per VERTEX in WORLD space, so the shader transforms the
 * vertex and its normal by the world matrix alone and does the arithmetic
 * there. Two things are deliberately NOT implemented and each says so where it
 * is dropped rather than being left to look applied: specular lighting (the
 * engine sets SPECULARENABLE=0) and spot cones, where a spot is treated as a
 * point light and reported as one.
 */
#include "d3d8_lighting.h"

#include "d3d8_light_dump.h"
#include "d3d8_light_survey.h"

#include "d3d8_device.h"
#include "d3d8_drawcall.h"
#include "d3d8_lightlog.h"
#include "d3d8_render_states.h"
#include "d3d8_types.h"

#include "gpu_device.h"
#include "gpu_matrix.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Render state with its D3D default, as d3d8_drawcall.c reads it. */
static uint32_t rs(const D3D8State *s, uint32_t which, uint32_t dflt) {
  return s->render[which].set ? s->render[which].value : dflt;
}

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
 * look applied: specular lighting (the engine sets SPECULARENABLE=0) and spot
 * cones (falloff/theta/phi -- a spot is treated as a point light and SAYS so).
 */
/* Draw-time light table vs what SetLight last wrote -- see fill_lighting. */
static unsigned long g_lc_checked, g_lc_differ, g_lc_lost, g_lc_neverset;

static void copy4(float *dst, const float *src) {
  memcpy(dst, src, 4 * sizeof *dst);
}

void d3d8_argb_to_rgba(uint32_t c, float *out) {
  out[0] = (float)((c >> 16) & 0xFF) / 255.0f;
  out[1] = (float)((c >> 8) & 0xFF) / 255.0f;
  out[2] = (float)((c) & 0xFF) / 255.0f;
  out[3] = (float)((c >> 24) & 0xFF) / 255.0f;
}

void d3d8_fill_lighting(const D3D8State *s, GpuDraw *out) {
  static const float ident[16] = {1, 0, 0, 0, 0, 1, 0, 0,
                                  0, 0, 1, 0, 0, 0, 0, 1};
  static int told_spot, told_toomany;
  const float *w =
      s->transform_set[D3DTS_WORLD] ? s->transform[D3DTS_WORLD].m : ident;
  unsigned i;

  memcpy(out->world, w, sizeof out->world);
  out->lighting = rs(s, D3DRS_LIGHTING, 0) != 0;
  out->color_vertex = rs(s, D3DRS_COLORVERTEX, 1) != 0;
  out->normalize_normals = rs(s, D3DRS_NORMALIZENORMALS, 0) != 0;
  out->diffuse_source = rs(s, D3DRS_DIFFUSEMATERIALSOURCE, 1);
  out->ambient_source = rs(s, D3DRS_AMBIENTMATERIALSOURCE, 0);
  out->emissive_source = rs(s, D3DRS_EMISSIVEMATERIALSOURCE, 0);
  d3d8_light_note_ambient(rs(s, D3DRS_AMBIENT, 0));
  d3d8_argb_to_rgba(rs(s, D3DRS_AMBIENT, 0), out->global_ambient);
  /* The survey has to see the UNLIT draws too, and this early return is why
     it could not: its "0 unlit" was true by construction, not measured -- a
     counter that can only ever print zero. */
  if (!out->lighting) {
    d3d8_light_survey(out);
    return;
  }

  if (s->material_set) {
    copy4(out->mat_diffuse, &s->material[0]);
    copy4(out->mat_ambient, &s->material[4]);
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
    if (!s->light_set[i] || !s->light_on[i])
      continue;
    if (out->nlights == GPU_MAX_LIGHTS) {
      if (!told_toomany++)
        x2_log_error("d3d8: more than %d lights are enabled at "
                     "once; the rest are DROPPED and the scene is "
                     "darker than the engine asked for.\n",
                     GPU_MAX_LIGHTS);
      break;
    }
    if (out->nlights < 8)
      d3d8_light_note_source(out->nlights, (int)i);
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
      float wrote[3];
      if (d3d8_last_setlight_diffuse(i, wrote)) {
        int same = fabsf(wrote[0] - g->diffuse[0]) < 1e-6f &&
                   fabsf(wrote[1] - g->diffuse[1]) < 1e-6f &&
                   fabsf(wrote[2] - g->diffuse[2]) < 1e-6f;
        int drawblack = g->diffuse[0] == 0.0f && g->diffuse[1] == 0.0f &&
                        g->diffuse[2] == 0.0f;
        int wroteblack =
            wrote[0] == 0.0f && wrote[1] == 0.0f && wrote[2] == 0.0f;
        g_lc_checked++;
        if (!same)
          g_lc_differ++;
        if (drawblack && !wroteblack) {
          g_lc_lost++;
          if (g_lc_lost <= 3)
            x2_log_error("d3d8: light %u reaches a draw BLACK, "
                         "but the last SetLight for that index wrote "
                         "%.3f %.3f %.3f. The colour is lost between "
                         "the two.\n",
                         i, wrote[0], wrote[1], wrote[2]);
        }
      } else {
        g_lc_neverset++;
      }
    }
    if (g->type == 2 && !told_spot++)
      x2_log_error("d3d8: a SPOT light is enabled; this stage has no "
                   "cone, so it is lit as a point light -- brighter "
                   "outside the cone than the engine asked for.\n");
  }
  {
    float wv[16];
    d3d8_worldview_transform(s, wv);
    d3d8_light_note_viewpos(wv[12], wv[13], wv[14]);
  }
  d3d8_light_dump(out);
  d3d8_light_survey(out);
}

static void light_table_report(void) {
  x2_log_info(
      "        light table vs SetLight: %lu enabled-light read(s) "
      "compared, %lu differ from what SetLight last wrote, %lu arrive "
      "BLACK at a draw although SetLight wrote a colour, %lu were never "
      "set at all\n",
      g_lc_checked, g_lc_differ, g_lc_lost, g_lc_neverset);
}

/*
 * Everything the lighting layer measured this run, in one place.
 *
 * The draw path used to call the dump, the table comparison and the survey
 * itself, which meant the run report had to know which diagnostics exist.
 */
void d3d8_lighting_report(void) {
  d3d8_light_dump_report();
  light_table_report();
  d3d8_light_survey_report();
}
