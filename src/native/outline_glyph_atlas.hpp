/* The outline typefaces that replace the retail font atlases' pixels: their
 * size-free layout metrics, and the glyph cache that rasterises each glyph at
 * the pixel size it is drawn at. See external/xmen2-decomp/docs/notes/text.md. */
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

/* Layout metrics are unhinted and in cap heights: the height of the face's
   reference glyph ('H', or '0' for digits), stroke excluded. */

/* The face's advance for `codepoint`; false when the face has no such glyph
   or its file cannot be read. */
bool outline_glyph_advance(OutlineFace face, std::uint16_t codepoint,
                           float *advance);

/* The face's kerning of `right` after `left` (negative draws them closer);
   0 for an unkerned pair or a face without kerning. */
float outline_glyph_kerning(OutlineFace face, std::uint16_t left,
                            std::uint16_t right);

/* The face's word-space advance; 0 when it has none. */
float outline_glyph_space(OutlineFace face);

/* The pixel size a glyph is rasterised at: its cap height in quarter
   pixels, its horizontal stretch in 64ths, and the pen's position within its
   pixel in quarters. */
struct OutlineRasterSize {
  std::uint16_t cap_quarters;
  std::uint16_t stretch_64ths;
  std::uint8_t phase;
};
inline constexpr unsigned kOutlinePhases = 4u;
inline constexpr unsigned kOutlineCapQuartersMax = 4u * 400u;

/* A glyph rasterised at one size: its texels in an atlas page, placed from
   the pen's whole pixel on the baseline (left: columns to its right, top:
   rows above it). A glyph without ink has no texels. */
struct OutlineRaster {
  std::uint16_t page, x, y, width, height;
  std::int16_t left, top;
};
inline constexpr unsigned kOutlinePageW = 1024u;
inline constexpr unsigned kOutlinePageH = 1024u;
inline constexpr unsigned kOutlinePagesMax = 8u;

/* `codepoint` of `face` at `size`, rasterised on first use; nullptr when the
   face has no such glyph or the pages have no room left this frame. */
const OutlineRaster *outline_glyph_raster(OutlineFace face,
                                          std::uint16_t codepoint,
                                          OutlineRasterSize size);

/* Pages in use, each kOutlinePageW x kOutlinePageH RGBA8. */
unsigned outline_glyph_pages();
const std::uint8_t *outline_glyph_page(unsigned page);

/* The rows of `page` written since the last call; false when none. */
bool outline_glyph_page_take_dirty(unsigned page, unsigned *first_row,
                                   unsigned *rows);

/* A new frame: pages that ran out of room in the last one start again
   empty, which no draw of this frame has sampled yet. */
void outline_glyph_frame_begin();

void outline_glyph_atlas_report();

} // namespace x2::native
