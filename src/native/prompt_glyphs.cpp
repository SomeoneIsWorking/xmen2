#include "prompt_glyphs.h"
#include "pad_glyph_codes.h"

#include <stdlib.h>
#include <string.h>

#include <lucent/cvar_c.h>

static uint8_t g_unavailable[X2_PROMPT_GLYPH_LAST - X2_PROMPT_GLYPH_FIRST + 1u];

int x2_prompt_glyphs_enabled(void) {
  static int enabled = -1;
  if (enabled < 0) {
    enabled = lucent_cvar_flag("prompt_glyphs", 1) != 0;
  }
  return enabled;
}

int x2_prompt_glyph_available(uint16_t codepoint) {
  unsigned index;
  if (codepoint < X2_PROMPT_GLYPH_FIRST || codepoint > X2_PROMPT_GLYPH_LAST)
    return 0;
  index = (unsigned)(codepoint - X2_PROMPT_GLYPH_FIRST);
  return !g_unavailable[index];
}

void x2_prompt_glyph_mark_unavailable(uint16_t codepoint) {
  unsigned index;
  if (codepoint < X2_PROMPT_GLYPH_FIRST || codepoint > X2_PROMPT_GLYPH_LAST)
    return;
  index = (unsigned)(codepoint - X2_PROMPT_GLYPH_FIRST);
  g_unavailable[index] = 1u;
}
