/*
 * Which controller family's art a prompt glyph shows.
 *
 * Every style shares one atlas layout (tools/render_prompt_glyphs.py), so a
 * style is only a choice of pixels and the GPU owner keeps one texture per
 * style. A pad name carries its own family as an invisible style marker
 * before the glyph (pad_glyphs.cpp), so each player's prompts follow that
 * player's controller; a glyph with no marker uses the default style.
 */
#ifndef X2_PROMPT_GLYPH_STYLE_H
#define X2_PROMPT_GLYPH_STYLE_H

#include <stdint.h>

enum {
  X2_PROMPT_STYLE_XBOX = 0,
  X2_PROMPT_STYLE_PLAYSTATION = 1,
  X2_PROMPT_STYLE_NINTENDO = 2,
  X2_PROMPT_STYLE_COUNT
};

/* `default_style`: for glyphs no pad name marked (player one's family, or the
   forced one). `forced_style`: the Button Icons setting when it names a family,
   which then applies to every pad; -1 under Automatic. */
void x2_prompt_set_style(int default_style, int forced_style);
int x2_prompt_style(void);
int x2_prompt_forced_style(void);
/* The RGBA atlas (X2_PROMPT_ATLAS_BYTES) for `style`; Xbox for any other value.
 */
const uint8_t *x2_prompt_style_pixels(int style);

#endif /* X2_PROMPT_GLYPH_STYLE_H */
