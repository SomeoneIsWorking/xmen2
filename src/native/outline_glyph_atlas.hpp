/* The outline typefaces that replace the retail font atlases' pixels, and the
 * one RGBA atlas they are rasterised into. See docs/RE/text.md. */
#pragma once

#include <cstdint>

namespace x2::native {

/* One typeface per retail font family (outline_text.cpp maps them). */
enum class OutlineFace : std::uint8_t {
  Italic,  /* menus, dialogs, captions: Barlow Black Italic */
  Upright, /* the HUD: Barlow Black */
  Digital, /* the timer digits: DSEG7 Classic Bold Italic */
};
inline constexpr unsigned kOutlineFaces = 3u;

/* A rasterised glyph. UVs are bottom-origin like every PromptQuad; the rest
   is in atlas pixels relative to the pen on the baseline, at kOutlineCapPx. */
struct OutlineCell {
  float u0, v0, u1, v1;
  float left, top, width, height, advance;
};

/* Height of the face's reference glyph ('H', or '0' for digits), stroke
   excluded. */
inline constexpr float kOutlineCapPx = 48.0f;
inline constexpr unsigned kOutlineAtlasW = 2048u;
inline constexpr unsigned kOutlineAtlasH = 2048u;
inline constexpr unsigned kOutlineAtlasLevels = 5u;

/* The cell for `codepoint` in `face`, rasterising the whole atlas on first
   use; nullptr when the face has no such glyph or its file cannot be read. */
const OutlineCell *outline_glyph_cell(OutlineFace face,
                                      std::uint16_t codepoint);

/* The face's kerning of `right` after `left`, in atlas pixels (negative
   draws them closer); 0 for an unkerned pair or a face without kerning. */
float outline_glyph_kerning(OutlineFace face, std::uint16_t left,
                            std::uint16_t right);

/* The face's word-space advance in atlas pixels; 0 when it has none. */
float outline_glyph_space(OutlineFace face);

/* Mip level `level` of the atlas (RGBA8, level 0 is kOutlineAtlasW wide), or
   nullptr when the atlas could not be built. */
const std::uint8_t *outline_glyph_atlas_level(unsigned level, unsigned *width,
                                              unsigned *height);

void outline_glyph_atlas_report();

} // namespace x2::native
