/* A composed keyboard keycap: its shape in a string, and its shared art. */
#pragma once

#include "prompt_glyph_quads.h"

#include <cstdint>

struct x2_keycap_art;

namespace x2::native {

/*
 * prompt_labels.cpp turns the retail "[NAME]" into
 *
 *   left edge, NAME, right edge
 *
 * The edges are private codepoints that carry layout only -- the margin either
 * side of the name -- so the game's own text layout reserves the key's width
 * and spaces the words after it. prompt_glyph_draw.cpp then draws the whole key
 * over that span from shared port-assets art: the blank cap stretched to it
 * and the name lettered in the shared key typeface (keycap_labels.h). The
 * game's letters inside the span draw nothing.
 */
inline constexpr unsigned kKeycapNameMax = 31u;

/* The length in characters of the keycap that opens at wide[at], edges
   included, or 0 when none does. The name is wide[at+1 .. at+length-2]. */
unsigned keycap_run_length(const uint16_t *wide, unsigned length, unsigned at);

/*
 * The whole key over one composed run, in the engine text plane. `left` and
 * `right` are the edge glyphs' own rectangles as the retail emitter placed
 * them (x0, y0, x1, y1); the key spans left.x0 .. right.x1 at the left edge's
 * height, which is the 18-design-pixel cap height. Out: the frame's left end,
 * stretched middle and right end, then the label, centred and narrowed to the
 * cap's lettering width when the name is wider than the layout left for it.
 */
inline constexpr unsigned kKeycapQuads = 4u;
void keycap_quads(const float left[4], const float right[4],
                  const struct x2_keycap_art *label, uint32_t color,
                  PromptQuad out[kKeycapQuads]);

} // namespace x2::native
