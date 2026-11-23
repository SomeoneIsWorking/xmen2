/* The generated atlas has one storage owner; consumers include declarations. */
#define X2_PROMPT_GLYPH_ATLAS_DEFINE
#include "prompt_glyph_atlas.h"
#include "prompt_glyph_style.h"

#include <atomic>

static std::atomic<int> g_style{X2_PROMPT_STYLE_XBOX};
static std::atomic<int> g_forced_style{-1};

void x2_prompt_set_style(int default_style, int forced_style) {
  g_style.store(default_style);
  g_forced_style.store(forced_style);
}

int x2_prompt_style(void) { return g_style.load(); }

int x2_prompt_forced_style(void) { return g_forced_style.load(); }

const uint8_t *x2_prompt_style_pixels(int style) {
  switch (style) {
  case X2_PROMPT_STYLE_PLAYSTATION:
    return x2_prompt_atlas_playstation;
  case X2_PROMPT_STYLE_NINTENDO:
    return x2_prompt_atlas_nintendo;
  default:
    return x2_prompt_atlas;
  }
}
