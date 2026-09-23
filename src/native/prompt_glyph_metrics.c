#include "x2_log.h"
/*
 * Layout metrics for the port's own prompt codepoints.
 *
 * ## Why the font record is written at all
 *
 * The renderer-side direction is that no GAME PIXEL is edited and no font
 * ASSET is copied: docs/RE/text.md. Metrics are a different thing from
 * pixels. The exe's glyph loop takes every layout decision from the font
 * record -- the pen advances by `word[glyph*0x1c+4]`, and the quad's size
 * comes from the record's width and height -- so a codepoint whose record is
 * blank occupies no space. Measured (C269): drawing a composed keycap label
 * emitted ELEVEN degenerate zero-size quads all at the same pen x, because
 * the private records carried nothing. The key's edges piled up on one
 * column and reserved no margin around the binding's name.
 *
 * Reproducing the engine's layout in the port instead was tried on paper and
 * rejected: the run is right-anchored, so its origin depends on the string's
 * own content (truncating a label from 17 to 13 to 12 wchars moved its first
 * quad from 19.022 to 39.289 to 46.756). Splitting a string and redrawing the
 * pieces therefore misplaces every piece unless the port re-derives that
 * anchoring exactly -- reimplementing the engine's layout to avoid writing
 * the metrics into records the game never uses.
 *
 * So the port publishes ITS OWN metrics for ITS OWN codepoints, in memory,
 * into records the shipped fonts leave empty, and the engine lays the label
 * out correctly. This is the same class of thing ui_text_scale.c already
 * does to every record in the table, at the same moment and the same scale.
 *
 * ## What is deliberately NOT written
 *
 * The UVs. `+0x0c..+0x1b` stay whatever the font had, so the stock drawer
 * still has no art of ours to sample -- and it never draws these quads at
 * all, because prompt_glyph_draw.c suppresses them at the emitter and the
 * port draws the art itself from its own atlas. Writing UVs here would be
 * the font-mediated route this direction exists to replace.
 */
#include "prompt_glyph_metrics.h"
#include "text_put.h"

#include "pad_glyph_codes.h"
#include "prompt_glyph_atlas.h"
#include "prompt_glyphs.h"
#include "x86rt.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* The exe's font record, from FUN_00596af0 -- same field map as
   ui_text_scale.c, which owns the authority for these offsets. */
#define GLYPH_FIRST 0x18u
#define GLYPH_STRIDE 0x1cu
#define GL_WIDTH 0x00u
#define GL_HEIGHT 0x02u
#define GL_ADVANCE 0x04u
#define GL_OFFSET 0x06u
#define GL_BASELINE 0x08u
#define GLYPH_COUNT 256u

static unsigned long g_records, g_cells_published;
static unsigned long g_records_occupied;
static unsigned long g_records_without_caps;

const struct x2_prompt_cell *x2_prompt_glyph_cell(uint16_t codepoint) {
  unsigned index;
  if (codepoint < X2_PROMPT_GLYPH_FIRST || codepoint > X2_PROMPT_GLYPH_LAST)
    return NULL;
  if (!x2_prompt_glyph_available(codepoint))
    return NULL;
  index = (unsigned)(codepoint - X2_PROMPT_GLYPH_FIRST);
  if (index >= X2_PROMPT_CELL_COUNT || !x2_prompt_cells[index].published)
    return NULL;
  return &x2_prompt_cells[index];
}

static int16_t scaled(int design, float scale) {
  return (int16_t)lrintf((float)design * scale);
}

/* A font's modal value of one glyph field, over its drawing glyphs in
   first..last. */
enum FontField { FONT_BASELINE, FONT_HEIGHT };

static int32_t glyph_field(uint32_t g, enum FontField field) {
  return field == FONT_BASELINE ? (int32_t)RD32(g + GL_BASELINE)
                                : (int32_t)RD16(g + GL_HEIGHT);
}

static int font_mode(uint32_t font_record, enum FontField field, unsigned first,
                     unsigned last, int32_t *result) {
  unsigned i, best_count = 0;
  int32_t best = 0;

  for (i = first; i <= last; i++) {
    uint32_t g = font_record + GLYPH_FIRST + i * GLYPH_STRIDE;
    int32_t candidate;
    unsigned j, count = 0;
    if (!RD16(g + GL_WIDTH) && !RD16(g + GL_HEIGHT))
      continue;
    candidate = glyph_field(g, field);
    if (!candidate)
      continue;
    for (j = first; j <= last; j++) {
      uint32_t other = font_record + GLYPH_FIRST + j * GLYPH_STRIDE;
      if ((!RD16(other + GL_WIDTH) && !RD16(other + GL_HEIGHT)) ||
          glyph_field(other, field) != candidate)
        continue;
      count++;
    }
    if (count > best_count) {
      best = candidate;
      best_count = count;
    }
  }
  if (!best_count)
    return 0;
  *result = best;
  return 1;
}

/* Where a font's prompts sit and how big they are both come from the font,
   not from an SVG or the text scale. Glyph boxes are tight, so the modal box
   of A..Z is the capital height (measured: 12, 16 and 21 in the three loaded
   fonts, with modal baselines 11, 15 and 20). Over the whole font the mode
   can be the accented capitals instead: 27 in the third. One design pixel is
   1/18 of that height, so the prompts beside dialog text are as large as that
   text, not as the smallest font's. Publishing zero for the baseline once put
   the keycap one full ascent below its stock letters. This runs after
   ui_text_scale, so both values are already in the drawer's units. */
typedef struct FontCaps {
  int32_t height, baseline;
} FontCaps;

static int font_caps(uint32_t font_record, FontCaps *caps) {
  return font_mode(font_record, FONT_HEIGHT, 'A', 'Z', &caps->height) &&
         font_mode(font_record, FONT_BASELINE, 0u, GLYPH_COUNT - 1u,
                   &caps->baseline);
}

void x2_prompt_glyph_publish_metrics(uint32_t font_record) {
  uint16_t code;
  unsigned published = 0, occupied = 0;
  char named[(X2_PROMPT_GLYPH_LAST - X2_PROMPT_GLYPH_FIRST + 1u) * 5u + 1u];
  size_t at = 0;
  FontCaps caps;
  float scale;

  if (!font_record)
    return;
  g_records++;
  named[0] = 0;
  /* Occupancy is authoritative even for a font whose baseline cannot be
     used for publication. A drawing record makes that byte non-private;
     baseline quality does not give the port permission to ignore it. The
     retail-font bytes the manifest already skips have no published cell and
     are not a collision. */
  for (code = X2_PROMPT_GLYPH_FIRST; code <= X2_PROMPT_GLYPH_LAST; code++) {
    uint32_t g = font_record + GLYPH_FIRST + (uint32_t)code * GLYPH_STRIDE;
    if (!x2_prompt_cells[code - X2_PROMPT_GLYPH_FIRST].published)
      continue;
    if (RD16(g + GL_WIDTH) || RD16(g + GL_HEIGHT)) {
      x2_prompt_glyph_mark_unavailable(code);
      text_put(named, sizeof named, &at, " 0x%02x", code);
      occupied++;
    }
  }
  if (occupied) {
    g_records_occupied++;
    x2_log_error("PROMPT METRICS: %u of the port's codepoints already "
                 "draw in this font (%s ) -- left alone and globally "
                 "unavailable to native prompt labels.\n",
                 occupied, named);
  }
  if (!font_caps(font_record, &caps)) {
    g_records_without_caps++;
    x2_log_error("PROMPT METRICS: font has no non-zero capital height and "
                 "baseline among its drawing glyphs -- publishing nothing "
                 "rather than sizing prompt art against a guessed line.\n");
    return;
  }
  scale = (float)caps.height / (float)X2_PROMPT_SOURCE_CELL_DESIGN;
  for (code = X2_PROMPT_GLYPH_FIRST; code <= X2_PROMPT_GLYPH_LAST; code++) {
    uint32_t g = font_record + GLYPH_FIRST + (uint32_t)code * GLYPH_STRIDE;
    /* The occupancy pass above already marked every retail-owned byte. */
    if (RD16(g + GL_WIDTH) || RD16(g + GL_HEIGHT))
      continue;
    const struct x2_prompt_cell *cell = x2_prompt_glyph_cell(code);
    if (!cell)
      continue;
    const int16_t height = scaled(cell->design_h, scale);
    WR16(g + GL_WIDTH, (uint16_t)scaled(cell->design_w, scale));
    WR16(g + GL_HEIGHT, (uint16_t)height);
    WR16(g + GL_ADVANCE, (uint16_t)scaled(cell->advance, scale));
    WR16(g + GL_OFFSET, 0);
    /* Centred on the capitals: a cell taller than them stands out evenly
       above and below. */
    WR32(g + GL_BASELINE,
         (uint32_t)(caps.baseline + (height - caps.height) / 2));
    published++;
  }
  g_cells_published += published;
  x2_log_error("PROMPT METRICS: published %u cell(s) into font record "
               "0x%08x at its capital height %d (scale %.3f) and modal "
               "baseline %d (metrics only, no UVs).\n",
               published, font_record, caps.height, (double)scale,
               caps.baseline);
}

void x2_prompt_glyph_metrics_report(void) {
  x2_log_info(
      "  Prompt metrics: %lu cell(s) published over %lu font record(s)"
      "; %lu had no evidenced capitals and baseline, %lu had a codepoint "
      "of ours already drawing\n",
      g_cells_published, g_records, g_records_without_caps, g_records_occupied);
  if (!g_records)
    x2_log_info("        no font record was ever offered, so the port's "
                "codepoints have NO metrics and every label will pile up in "
                "one column.\n");
}
