/* Prompt glyphs at the text renderer -- see prompt_glyph_draw.c. */
#ifndef X2_PROMPT_GLYPH_DRAW_H
#define X2_PROMPT_GLYPH_DRAW_H

#include <stdint.h>

/* True if the NUL-terminated guest wide string carries one of the port's
 * private prompt codepoints within the first max elements. */
int x2_string_has_prompt_glyph(uint32_t s_guest, unsigned max);

/* Does the retail glyph loop draw a quad for this wide character?
 *
 * The drawer's own exceptions, read out of the body (docs/RE/text.md): a
 * space and a tab advance the pen without drawing, colour tokens and absolute
 * pen sets draw nothing, and only wchar < 256 reaches the glyph path. Every
 * owner that walks a string alongside the emitter asks THIS, because two
 * copies of the rule drift apart and a drifted cursor intercepts the wrong
 * glyph. */
int x2_glyph_loop_emits_quad(uint16_t c);

/* One line at shutdown, with denominators: strings scanned vs strings that
 * carried prompt codepoints. A run in which no text drew must not read like
 * a run in which prompts drew without them. */
void x2_prompt_draw_report(void);

#endif /* X2_PROMPT_GLYPH_DRAW_H */
