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
 * four numbers into a record the game never uses.
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
#include "x86rt.h"

#include <math.h>
#include <stdio.h>

/* The exe's font record, from FUN_00596af0 -- same field map as
   ui_text_scale.c, which owns the authority for these offsets. */
#define GLYPH_FIRST   0x18u
#define GLYPH_STRIDE  0x1cu
#define GL_WIDTH      0x00u
#define GL_HEIGHT     0x02u
#define GL_ADVANCE    0x04u
#define GL_OFFSET     0x06u

static unsigned long g_records, g_cells_published;
static unsigned long g_records_occupied;

const struct x2_prompt_cell *x2_prompt_glyph_cell(uint16_t codepoint)
{
    unsigned index;
    if (codepoint < X2_PROMPT_GLYPH_FIRST || codepoint > X2_PROMPT_GLYPH_LAST)
        return NULL;
    index = (unsigned)(codepoint - X2_PROMPT_GLYPH_FIRST);
    if (index >= X2_PROMPT_CELL_COUNT) return NULL;
    return &x2_prompt_cells[index];
}

static int16_t scaled(int design, float scale)
{
    return (int16_t)lrintf((float)design * scale);
}

void x2_prompt_glyph_publish_metrics(uint32_t font_record, float scale)
{
    uint16_t code;
    unsigned published = 0, occupied = 0;

    if (!font_record) return;
    g_records++;
    for (code = X2_PROMPT_GLYPH_FIRST; code <= X2_PROMPT_GLYPH_LAST; code++) {
        const struct x2_prompt_cell *cell = x2_prompt_glyph_cell(code);
        uint32_t g = font_record + GLYPH_FIRST + (uint32_t)code * GLYPH_STRIDE;
        if (!cell) continue;
        /* A shipped font that already draws something here is NOT ours to
           overwrite: that would be editing the game's own glyph. Counted and
           skipped, loudly, because silently declining would look exactly like
           a publish that worked. */
        if (RD16(g + GL_WIDTH) || RD16(g + GL_HEIGHT)) {
            occupied++;
            continue;
        }
        WR16(g + GL_WIDTH, (uint16_t)scaled(cell->design_w, scale));
        WR16(g + GL_HEIGHT, (uint16_t)scaled(cell->design_h, scale));
        WR16(g + GL_ADVANCE, (uint16_t)scaled(cell->advance, scale));
        WR16(g + GL_OFFSET, 0);
        published++;
    }
    g_cells_published += published;
    if (occupied) {
        g_records_occupied++;
        fprintf(stderr, "PROMPT METRICS: %u of the port's codepoints already "
                        "draw in this font -- left alone, so the label will "
                        "lay out wrong rather than overwrite a game glyph.\n",
                occupied);
    }
    if (g_records == 1)
        fprintf(stderr, "PROMPT METRICS: published %u cell(s) at scale %.3f "
                        "into the first font record (metrics only, no UVs).\n",
                published, (double)scale);
}

void x2_prompt_glyph_metrics_report(void)
{
    printf("  Prompt metrics: %lu cell(s) published over %lu font record(s)"
           "; %lu record(s) had a codepoint of ours already drawing\n",
           g_cells_published, g_records, g_records_occupied);
    if (!g_records)
        printf("        no font record was ever offered, so the port's "
               "codepoints have NO metrics and every label will pile up in "
               "one column.\n");
}
