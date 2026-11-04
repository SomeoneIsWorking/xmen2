/* Retail font records drawn with outline glyphs: which typeface each font
 * gets, its scale against the record's own metrics, and the quad that
 * replaces one retail glyph. See docs/RE/text.md. */
#pragma once

#include "outline_glyph_atlas.hpp"
#include "prompt_glyph_quads.h"

#include <cstdint>
#include <vector>

namespace x2::native {

/* A loaded retail font and how its outline glyphs are sized, in the record's
   (already text-scaled) units per cap height. */
struct OutlineFont {
  std::uint32_t record = 0;
  OutlineFace face = OutlineFace::Italic;
  float caps_scale = 0.0f;  /* capitals and every non-small-caps glyph */
  float small_scale = 0.0f; /* lowercase drawn as small capitals; 0 if none */
  float width_scale = 1.0f; /* fits the face's advances to the record's */
};

/* The text plane per record unit along x and y, signed as the engine lays the
   glyph box out (y runs from the box's top). */
struct OutlinePlaneScale {
  float x, y;
};

/* The `outline_text` cvar. */
bool outline_text_enabled();

/* FUN_00596af0 filled `record` from ui/fonts/<name>.xmlb and ui_text_scale
   has scaled it; also called after every rescale of a loaded record. */
void outline_text_font_loaded(std::uint32_t record, const char *name);
void outline_text_font_rescaled(std::uint32_t record);

/* The registered font FUN_005ee780 draws its string in, read from the
   owner it is called on; nullptr when that font has no outline face. */
const OutlineFont *outline_text_string_font(std::uint32_t owner);

/* Does the outline face draw `c` for this font? Icons sharing the font's
   Latin-1 slots and glyphs the record leaves empty stay retail. */
bool outline_text_replaces(const OutlineFont &font, std::uint16_t c);

/* An outline glyph placed, with the vertex key of the retail glyph it
   replaces. */
struct OutlinePlaced {
  PromptVertexKey key;
  PromptQuad quad;
};

/* Places one string's outline glyphs a run at a time, as pens and cap
   heights in the plane (PromptQuad). A run is a stretch of
   outline glyphs and word spaces on one line; a keycap, a colour token, a
   glyph the face does not draw or a new line ends it. The run starts at its
   first glyph's retail pen, steps by the face's advances, kerning and space,
   and is stretched once to end where retail's advances end it, so whatever
   follows the run keeps its retail position. */
class OutlinePen {
public:
  /* A new string, at this guest address, is being drawn. */
  void begin(std::uint32_t string);
  /* Queue the glyph at `index` in the string, from the retail emitter's box
     for it (x0, y0, x1, y1); false when it stays retail. A glyph that does
     not continue the open run closes that run first. */
  bool add(const OutlineFont &font, unsigned index, const float box[4],
           OutlinePlaneScale scale, PromptVertexKey key, std::uint32_t color);
  /* Close the open run: its glyphs move to the placed list. */
  void close();
  /* The glyphs of the runs closed so far, in layout order. */
  const std::vector<OutlinePlaced> &placed() const { return placed_; }
  void clear_placed() { placed_.clear(); }

private:
  struct Pending {
    OutlinePlaced placed;
    float pen;       /* the glyph's retail pen, in plane units */
    float advance;   /* its retail advance, in plane units */
    float travelled; /* face steps before it in the run, in record units */
    float step;      /* its own face advance, in record units */
    float scale;     /* plane units per record unit along x */
  };

  bool continues(unsigned index, float line, float scale_y) const;

  std::uint32_t string_ = 0;
  unsigned next_ = 0;      /* the index after the open run's last glyph */
  float line_ = 0.0f;      /* the open run's baseline, in plane units */
  float travelled_ = 0.0f; /* face steps so far, kerning included */
  float space_ = 0.0f;     /* the open run's face space, in record units */
  std::vector<Pending> open_;
  std::vector<OutlinePlaced> placed_;
};

void outline_text_report();

} // namespace x2::native
