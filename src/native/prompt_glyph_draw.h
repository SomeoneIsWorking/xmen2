/* Prompt glyphs at the text renderer -- see prompt_glyph_draw.cpp. */
#pragma once

#include <cstdint>

namespace x2::native {

/* Does the retail glyph loop draw a quad for this wide character?
 *
 * The drawer's own exceptions, read out of the body (docs/RE/text.md): a
 * space and a tab advance the pen without drawing, colour tokens and absolute
 * pen sets draw nothing, and only wchar < 256 reaches the glyph path. Every
 * owner that walks a string alongside the emitter asks THIS, because two
 * copies of the rule drift apart and a drifted cursor intercepts the wrong
 * glyph. */
int glyph_loop_emits_quad(uint16_t c);

/* The shutdown lines, with denominators: the string census, then quads
 * intercepted vs emitted and every refusal by reason. A run in which no text
 * drew must not read like a run in which prompts drew without them. */
void prompt_draw_report(void);

} // namespace x2::native
