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
 * emits ELEVEN degenerate zero-size quads all at the same pen x, because
 * records 0x90..0x93 carry nothing. The keycap pieces pile up on one column
 * and the binding letters do not sit inside anything.
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

#include "pad_glyph_codes.h"
#include "prompt_glyph_atlas.h"
#include "prompt_glyphs.h"
#include "x86rt.h"

#include <math.h>
#include <stdio.h>

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
static unsigned long g_records_without_baseline;

const struct x2_prompt_cell *x2_prompt_glyph_cell(uint16_t codepoint) {
  unsigned index;
  if (codepoint < X2_PROMPT_GLYPH_FIRST || codepoint > X2_PROMPT_GLYPH_LAST)
    return NULL;
  if (!x2_prompt_glyph_available(codepoint))
    return NULL;
  index = (unsigned)(codepoint - X2_PROMPT_GLYPH_FIRST);
  if (index >= X2_PROMPT_CELL_COUNT)
    return NULL;
  return &x2_prompt_cells[index];
}

static int16_t scaled(int design, float scale) {
  return (int16_t)lrintf((float)design * scale);
}

/* The baseline is owned by the font, not by an SVG. The retired font-pack
   path found it by majority across the shipped glyphs (11 in the unscaled HD
   font). Publishing zero here put the otherwise-correct keycap one full
   ascent below its stock letters. This runs after ui_text_scale, so the modal
   value copied from the retail records is already in the drawer's units. */
static int font_baseline(uint32_t font_record, int32_t *result) {
  unsigned i, best_count = 0;
  int32_t best = 0;

  for (i = 0; i < GLYPH_COUNT; i++) {
    uint32_t g = font_record + GLYPH_FIRST + i * GLYPH_STRIDE;
    int32_t candidate;
    unsigned j, count = 0;
    if (!RD16(g + GL_WIDTH) && !RD16(g + GL_HEIGHT))
      continue;
    candidate = (int32_t)RD32(g + GL_BASELINE);
    if (!candidate)
      continue;
    for (j = 0; j < GLYPH_COUNT; j++) {
      uint32_t other = font_record + GLYPH_FIRST + j * GLYPH_STRIDE;
      if ((!RD16(other + GL_WIDTH) && !RD16(other + GL_HEIGHT)) ||
          (int32_t)RD32(other + GL_BASELINE) != candidate)
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

void x2_prompt_glyph_publish_metrics(uint32_t font_record, float scale) {
  uint16_t code;
  unsigned published = 0, occupied = 0;
  int32_t baseline;

  if (!font_record)
    return;
  g_records++;
  /* Occupancy is authoritative even for a font whose baseline cannot be
     used for publication. A drawing record makes that byte non-private;
     baseline quality does not give the port permission to ignore it. */
  for (code = X2_PROMPT_GLYPH_FIRST; code <= X2_PROMPT_GLYPH_LAST; code++) {
    uint32_t g = font_record + GLYPH_FIRST + (uint32_t)code * GLYPH_STRIDE;
    if (RD16(g + GL_WIDTH) || RD16(g + GL_HEIGHT)) {
      x2_prompt_glyph_mark_unavailable(code);
      occupied++;
    }
  }
  if (occupied) {
    g_records_occupied++;
    fprintf(stderr,
            "PROMPT METRICS: %u of the port's codepoints already "
            "draw in this font -- left alone and globally "
            "unavailable to native prompt labels.\n",
            occupied);
  }
  if (!font_baseline(font_record, &baseline)) {
    g_records_without_baseline++;
    fprintf(stderr, "PROMPT METRICS: font has no non-zero baseline among "
                    "its drawing glyphs -- publishing nothing rather "
                    "than placing prompt art against a guessed line.\n");
    return;
  }
  for (code = X2_PROMPT_GLYPH_FIRST; code <= X2_PROMPT_GLYPH_LAST; code++) {
    uint32_t g = font_record + GLYPH_FIRST + (uint32_t)code * GLYPH_STRIDE;
    /* The occupancy pass above already marked every retail-owned byte. */
    if (RD16(g + GL_WIDTH) || RD16(g + GL_HEIGHT))
      continue;
    const struct x2_prompt_cell *cell = x2_prompt_glyph_cell(code);
    if (!cell)
      continue;
    WR16(g + GL_WIDTH, (uint16_t)scaled(cell->design_w, scale));
    WR16(g + GL_HEIGHT, (uint16_t)scaled(cell->design_h, scale));
    WR16(g + GL_ADVANCE, (uint16_t)scaled(cell->advance, scale));
    WR16(g + GL_OFFSET, 0);
    WR32(g + GL_BASELINE, (uint32_t)baseline);
    published++;
  }
  g_cells_published += published;
  if (g_records == 1)
    fprintf(stderr,
            "PROMPT METRICS: published %u cell(s) at scale %.3f "
            "and the font's modal baseline %d into the first font "
            "record (metrics only, no UVs).\n",
            published, (double)scale, baseline);
}

void x2_prompt_glyph_metrics_report(void) {
  printf("  Prompt metrics: %lu cell(s) published over %lu font record(s)"
         "; %lu had no evidenced baseline, %lu had a codepoint of ours "
         "already drawing\n",
         g_cells_published, g_records, g_records_without_baseline,
         g_records_occupied);
  if (!g_records)
    printf("        no font record was ever offered, so the port's "
           "codepoints have NO metrics and every label will pile up in "
           "one column.\n");
}
