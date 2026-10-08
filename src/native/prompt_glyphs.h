/* Runtime feature gate for native prompt-label composition and SVG drawing. */
#pragma once

#include <cstdint>

namespace x2::native {

int prompt_glyphs_enabled(void);

/* A codepoint remains private only while every loaded retail font leaves its
   record empty. Once any font owns it, all native prompt producers must stop
   using it: native art paired with a foreign font's metrics is not ours. */
int prompt_glyph_available(uint16_t codepoint);
void prompt_glyph_mark_unavailable(uint16_t codepoint);

} // namespace x2::native
