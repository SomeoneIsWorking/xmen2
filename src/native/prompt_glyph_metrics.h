/* Layout metrics for the port's own prompt codepoints. See the .c. */
#pragma once

#include <cstdint>

struct x2_prompt_cell;

namespace x2::native {

/* Publish width/height/advance for X2_PROMPT_GLYPH_FIRST..LAST into a font
   record the loader has just filled and ui_text_scale has scaled, sized to
   that font's own capitals. METRICS ONLY: no UVs are written, so the stock
   drawer still has nothing to sample -- the pixels come from the port. */
void prompt_glyph_publish_metrics(uint32_t font_record);

/* Design-space cell for an available private prompt codepoint, or NULL when
   it is outside the atlas or any loaded retail font already owns the byte. */
const struct x2_prompt_cell *prompt_glyph_cell(uint16_t codepoint);

void prompt_glyph_metrics_report(void);

} // namespace x2::native
