/* Harvested prompt-glyph rectangles. See prompt_glyph_draw.cpp. */
#pragma once

#include <cstdint>

namespace x2::native {

/* One prompt glyph in the ENGINE text plane. The stock sink stores each
   corner as (x, 0, y), and the owning text batch supplies the transform.
   u0..v1 are the port atlas's bottom-origin UVs; color is the D3DCOLOR
   captured from the engine's text batch. */
struct PromptQuad {
  float x0, y0, x1, y1;
  float u0, v0, u1, v1;
  std::uint32_t color;
  std::uint16_t codepoint;
  std::uint8_t sheet; /* X2_KEYCAP_SHEET_*: the atlas, or the key label sheet */
};

/* Where the engine wrote the collapsed glyph a quad replaces: the text
   batch's current vertex array and the index of the glyph's first vertex in
   it. The draw that submits that array range is the one that places it. */
struct PromptVertexKey {
  std::uint32_t vertex_array;
  std::uint32_t vertex;
};

inline constexpr unsigned kPromptQuadsMax = 512u;

/* The glyph count a non-indexed text draw of `primitives` submits, or 0 if
   it is not a run of whole glyphs. */
unsigned prompt_draw_glyphs(std::uint32_t primitives);

/* Drop every quad: a new frame. Quads never drawn are counted. */
void prompt_quads_reset();
/* Remaining quad slots; a producer checks this before it enters the retail
   loop so native interception is all-or-nothing. */
unsigned prompt_quads_available();
/* Retain `count` quads replacing the glyph the engine writes at `key`. A
   pending quad at the same key is stale -- its vertices are being written
   over -- and is dropped first. 1 means all were retained; a producer
   reserves capacity for its whole string first, so 0 is an invariant
   failure. */
int prompt_quads_put(struct PromptVertexKey key, const struct PromptQuad *quads,
                     unsigned count);
/* Take every pending quad whose glyph lies in `vertices` vertices of
   `vertex_array` from `start` -- the range a draw submits -- in the order
   they were laid out, and drop them. `out` holds kPromptQuadsMax. */
unsigned prompt_quads_take_range(std::uint32_t vertex_array,
                                 std::uint32_t start, std::uint32_t vertices,
                                 struct PromptQuad *out);
/* Pending quads. */
unsigned prompt_quads_pending();
void prompt_quads_report();

} // namespace x2::native
