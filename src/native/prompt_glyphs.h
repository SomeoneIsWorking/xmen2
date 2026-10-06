/* Runtime feature gate for native prompt-label composition and SVG drawing. */
#ifndef X2_PROMPT_GLYPHS_H
#define X2_PROMPT_GLYPHS_H

#include <stdint.h>

int x2_prompt_glyphs_enabled(void);

/* A codepoint remains private only while every loaded retail font leaves its
   record empty. Once any font owns it, all native prompt producers must stop
   using it: native art paired with a foreign font's metrics is not ours. */
int x2_prompt_glyph_available(uint16_t codepoint);
void x2_prompt_glyph_mark_unavailable(uint16_t codepoint);

#endif
