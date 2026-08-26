/* Layout metrics for the port's own prompt codepoints. See the .c. */
#ifndef X2_PROMPT_GLYPH_METRICS_H
#define X2_PROMPT_GLYPH_METRICS_H

#include <stdint.h>

/* Publish width/height/advance for X2_PROMPT_GLYPH_FIRST..LAST into a font
   record the loader has just filled, at the same text scale the rest of the
   record was scaled by. METRICS ONLY: no UVs are written, so the stock
   drawer still has nothing to sample -- the pixels come from the port. */
void x2_prompt_glyph_publish_metrics(uint32_t font_record, float scale);

/* Design-space cell for an available private prompt codepoint, or NULL when
   it is outside the atlas or any loaded retail font already owns the byte. */
const struct x2_prompt_cell *x2_prompt_glyph_cell(uint16_t codepoint);

void x2_prompt_glyph_metrics_report(void);

#endif
