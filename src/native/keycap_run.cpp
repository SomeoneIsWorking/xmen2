#include "keycap_run.h"

#include "pad_glyph_codes.h"
#include "prompt_glyph_atlas.h"
#include "prompt_glyph_quads.h"

#include <math.h>

/* The cap's height in design pixels, and the lettering margin either side
   of a fitted label, in the same units. */
#define CAP_DESIGN_HEIGHT ((float)X2_PROMPT_SOURCE_CELL_DESIGN)
#define LABEL_MARGIN_DESIGN                                                    \
  ((float)X2_KEYCAP_LABEL_MARGIN * CAP_DESIGN_HEIGHT /                         \
   (float)X2_KEYCAP_CAP_UNITS)

static int name_char(uint16_t c) { return c > 0x20u && c < 0x7fu; }

unsigned x2_keycap_run_length(const uint16_t *wide, unsigned length,
                              unsigned at) {
  unsigned end;

  if (!wide || at >= length || wide[at] != X2_KEYCAP_GLYPH_LEFT) {
    return 0;
  }
  for (end = at + 1u; end < length && name_char(wide[end]); end++) {
  }
  if (end == at + 1u || end - at - 1u > X2_KEYCAP_NAME_MAX || end == length ||
      wide[end] != X2_KEYCAP_GLYPH_RIGHT) {
    return 0;
  }
  return end - at + 1u;
}

static struct X2PromptQuad art_quad(const struct x2_keycap_art *art, float x0,
                                    float x1, float y0, float y1,
                                    uint32_t color) {
  struct X2PromptQuad q;
  q.x0 = x0;
  q.y0 = y0;
  q.x1 = x1;
  q.y1 = y1;
  q.u0 = art->u0;
  q.v0 = art->v0;
  q.u1 = art->u1;
  q.v1 = art->v1;
  q.color = color;
  q.codepoint = X2_KEYCAP_GLYPH_LEFT;
  q.sheet = art->sheet;
  return q;
}

void x2_keycap_quads(const float left[4], const float right[4],
                     const struct x2_keycap_art *label, uint32_t color,
                     struct X2PromptQuad out[X2_KEYCAP_QUADS]) {
  const float x0 = left[0], x1 = right[2], y0 = left[1], y1 = left[3];
  const float dir = x1 >= x0 ? 1.0f : -1.0f;
  const float span = fabsf(x1 - x0);
  const float unit = fabsf(y1 - y0) / CAP_DESIGN_HEIGHT;
  const float fit = fmaxf(span - 2.0f * LABEL_MARGIN_DESIGN * unit, 0.0f);
  const float text = fminf(label->design_w * unit, fit);
  const float centre = 0.5f * (x0 + x1);
  float edge = x2_keycap_frame[X2_KEYCAP_FRAME_LEFT].design_w * unit;

  if (2.0f * edge > span) {
    edge = 0.5f * span;
  }
  out[0] = art_quad(&x2_keycap_frame[X2_KEYCAP_FRAME_LEFT], x0, x0 + dir * edge,
                    y0, y1, color);
  out[1] = art_quad(&x2_keycap_frame[X2_KEYCAP_FRAME_MIDDLE], x0 + dir * edge,
                    x1 - dir * edge, y0, y1, color);
  out[2] = art_quad(&x2_keycap_frame[X2_KEYCAP_FRAME_RIGHT], x1 - dir * edge,
                    x1, y0, y1, color);
  out[3] = art_quad(label, centre - dir * 0.5f * text,
                    centre + dir * 0.5f * text, y0, y1, color);
}
