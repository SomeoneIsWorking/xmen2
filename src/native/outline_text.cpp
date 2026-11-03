#include "outline_text.hpp"

#include "guest_memory.h"
#include "x2_log.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include <lucent/cvar_c.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <vector>

namespace x2::native {

namespace {

/* XMen2.exe's font table (FUN_00597c20) and its records (FUN_00596af0). */
constexpr std::uint32_t kFontTable = 0x008161f8u;
constexpr std::uint32_t kLinkedBase = 0x00400000u;
constexpr std::uint32_t kFontStride = 0x1c18u;
/* The glyph loop's owner keeps the font index it draws in here (0x005ee7b3). */
constexpr std::uint32_t kOwnerFontIndex = 0x2140cu;
constexpr std::uint32_t kGlyphFirst = 0x18u;
constexpr std::uint32_t kGlyphStride = 0x1cu;
constexpr std::uint32_t kGlWidth = 0x00u, kGlHeight = 0x02u, kGlAdvance = 0x04u,
                        kGlOffset = 0x06u, kGlBaseline = 0x08u;
constexpr unsigned kFontsMax = 16u;
/* The retail rim is one texel round a capital whose ink, rim included, is
   its baseline: the fill is 14 of those 16 rows. */
constexpr float kStrokedFillShare = 14.0f / 16.0f;
constexpr float kWidthScaleMin = 0.7f, kWidthScaleMax = 1.4f;

struct Glyph {
  int width, height, advance, offset, baseline;
};

Glyph read_glyph(std::uint32_t record, std::uint16_t c) {
  const std::uint32_t g =
      record + kGlyphFirst + (std::uint32_t)c * kGlyphStride;
  return {(std::int16_t)RD16(g + kGlWidth), (std::int16_t)RD16(g + kGlHeight),
          (std::int16_t)RD16(g + kGlAdvance), (std::int16_t)RD16(g + kGlOffset),
          (std::int32_t)RD32(g + kGlBaseline)};
}

bool drawn(const Glyph &g) { return g.width && g.height; }

/* The modal baseline over the drawn glyphs first..last; 0 when none. */
int modal_baseline(std::uint32_t record, std::uint16_t first,
                   std::uint16_t last) {
  std::array<unsigned, 256> counts{};
  int best = 0;
  unsigned best_count = 0;
  for (unsigned c = first; c <= last; c++) {
    const Glyph g = read_glyph(record, (std::uint16_t)c);
    if (!drawn(g) || g.baseline <= 0 || g.baseline >= 256) {
      continue;
    }
    if (++counts[(unsigned)g.baseline] > best_count) {
      best_count = counts[(unsigned)g.baseline];
      best = g.baseline;
    }
  }
  return best;
}

bool lowercase(std::uint16_t c) {
  return (c >= 'a' && c <= 'z') || (c >= 0xe0u && c <= 0xfeu && c != 0xf7u);
}

/* Printable ASCII and the Latin-1 letters; the fonts keep icons in the other
   Latin-1 slots (0xa4..0xbe in the medium font). */
bool text_codepoint(std::uint16_t c) {
  return (c >= 0x21u && c <= 0x7eu) || c == 0xa1u || c == 0xbfu ||
         (c >= 0xc0u && c <= 0xffu);
}

bool face_for_name(const char *name, OutlineFace *face) {
  char lower[64];
  std::size_t i = 0;
  for (; name && name[i] && i + 1 < sizeof lower; i++) {
    lower[i] = (char)std::tolower((unsigned char)name[i]);
  }
  lower[i] = 0;
  if (std::strstr(lower, "digital")) {
    *face = OutlineFace::Digital;
  } else if (std::strstr(lower, "hud")) {
    *face = OutlineFace::Upright;
  } else if (std::strstr(lower, "med") || std::strstr(lower, "thin") ||
             std::strstr(lower, "big")) {
    *face = OutlineFace::Italic;
  } else {
    return false;
  }
  return true;
}

class Registry {
public:
  void loaded(std::uint32_t record, const char *name) {
    OutlineFace face;
    if (!face_for_name(name, &face)) {
      x2_log_error("OUTLINE TEXT: font %s has no outline face; it keeps the "
                   "retail glyphs.\n",
                   name ? name : "(unnamed)");
      return;
    }
    OutlineFont *slot = find(record);
    if (!slot && count_ < kFontsMax) {
      slot = &fonts_[count_++];
    }
    if (!slot) {
      refused_++;
      return;
    }
    *slot = OutlineFont{};
    slot->record = record;
    slot->face = face;
    if (!derive(slot)) {
      x2_log_error("OUTLINE TEXT: font %s at 0x%08x has no capitals or digits "
                   "to size its face by; it keeps the retail glyphs.\n",
                   name, record);
      slot->caps_scale = 0.0f;
      return;
    }
    x2_log_error("OUTLINE TEXT: font %s at 0x%08x drawn in face %u, %.4f "
                 "units per pixel (small capitals %.4f), width %.3f.\n",
                 name, record, (unsigned)face, (double)slot->caps_scale,
                 (double)slot->small_scale, (double)slot->width_scale);
  }

  void rescaled(std::uint32_t record) {
    OutlineFont *slot = find(record);
    if (slot && !derive(slot)) {
      slot->caps_scale = 0.0f;
    }
  }

  const OutlineFont *string_font(std::uint32_t owner) {
    std::uint32_t index = 0;
    lookups_++;
    if (!guest_memory_try_read32(owner + kOwnerFontIndex, &index) ||
        index >= kFontsMax) {
      unreadable_++;
      return nullptr;
    }
    const std::uint32_t table =
        kFontTable - kLinkedBase + x86_module_base("XMen2.exe");
    const OutlineFont *font = find(table + index * kFontStride);
    if (!font || font->caps_scale <= 0.0f) {
      unregistered_++;
      return nullptr;
    }
    return font;
  }

  void report() const {
    x2_log_info("  Outline text: %u font(s) registered (%lu past the cap); "
                "%lu string font lookup(s), %lu with no readable owner, %lu "
                "in a font without an outline face\n",
                count_, refused_, lookups_, unreadable_, unregistered_);
  }

private:
  OutlineFont *find(std::uint32_t record) {
    for (unsigned i = 0; i < count_; i++) {
      if (fonts_[i].record == record) {
        return &fonts_[i];
      }
    }
    return nullptr;
  }

  static bool derive(OutlineFont *font) {
    const bool stroked = font->face != OutlineFace::Digital;
    const float fill = stroked ? kStrokedFillShare : 1.0f;
    int caps = modal_baseline(font->record, 'A', 'Z');
    if (!caps) {
      caps = modal_baseline(font->record, '0', '9');
    }
    if (!caps) {
      return false;
    }
    font->caps_scale = (float)caps * fill / kOutlineCapPx;
    /* Small capitals: an ascender sits no higher than an x-height letter. */
    const Glyph b = read_glyph(font->record, 'b');
    const Glyph x = read_glyph(font->record, 'x');
    font->small_scale = 0.0f;
    if (drawn(b) && drawn(x) && b.baseline == x.baseline) {
      font->small_scale =
          (float)modal_baseline(font->record, 'a', 'z') * fill / kOutlineCapPx;
    }
    float retail = 0.0f, outline = 0.0f;
    for (unsigned c = 0x21u; c <= 0x7eu; c++) {
      const Glyph g = read_glyph(font->record, (std::uint16_t)c);
      const bool small =
          font->small_scale > 0.0f && lowercase((std::uint16_t)c);
      const OutlineCell *cell = outline_glyph_cell(
          font->face, small ? (std::uint16_t)(c - 0x20u) : (std::uint16_t)c);
      if (!drawn(g) || !cell || !std::isalnum((int)c)) {
        continue;
      }
      retail += (float)g.advance;
      outline += cell->advance * (small ? font->small_scale : font->caps_scale);
    }
    if (outline <= 0.0f) {
      return false;
    }
    font->width_scale =
        std::clamp(retail / outline, kWidthScaleMin, kWidthScaleMax);
    return true;
  }

  std::array<OutlineFont, kFontsMax> fonts_{};
  unsigned count_ = 0;
  unsigned long refused_ = 0, lookups_ = 0, unreadable_ = 0, unregistered_ = 0;
};

Registry g_registry;
int g_enabled = -1;

std::uint16_t drawn_code(const OutlineFont &font, std::uint16_t c) {
  if (font.small_scale > 0.0f && lowercase(c)) {
    return (std::uint16_t)(c - 0x20u);
  }
  return c;
}

/* The face's cell for `c` and the record units per atlas pixel it is drawn
   at; nullptr when `c` stays retail. */
const OutlineCell *face_cell(const OutlineFont &font, std::uint16_t c,
                             float *units) {
  if (!text_codepoint(c) || !drawn(read_glyph(font.record, c))) {
    return nullptr;
  }
  const std::uint16_t code = drawn_code(font, c);
  *units = code != c ? font.small_scale : font.caps_scale;
  return outline_glyph_cell(font.face, code);
}

/* The face's pen step from `c` to `next` in record units: its advance and
   the pair's kerning, or the advance alone when `next` stays retail. */
float face_step(const OutlineFont &font, std::uint16_t c, std::uint16_t next) {
  float units = 0.0f, next_units = 0.0f;
  const OutlineCell *cell = face_cell(font, c, &units);
  if (!cell) {
    return 0.0f;
  }
  float step = cell->advance * units;
  if (face_cell(font, next, &next_units)) {
    step += outline_glyph_kerning(font.face, drawn_code(font, c),
                                  drawn_code(font, next)) *
            std::min(units, next_units);
  }
  return step * font.width_scale;
}

} // namespace

bool outline_text_enabled() {
  if (g_enabled < 0) {
    g_enabled = lucent_cvar_flag("outline_text", 1) != 0;
  }
  return g_enabled != 0;
}

void outline_text_font_loaded(std::uint32_t record, const char *name) {
  g_registry.loaded(record, name);
}

void outline_text_font_rescaled(std::uint32_t record) {
  g_registry.rescaled(record);
}

const OutlineFont *outline_text_string_font(std::uint32_t owner) {
  return g_registry.string_font(owner);
}

bool outline_text_replaces(const OutlineFont &font, std::uint16_t c) {
  float units = 0.0f;
  return face_cell(font, c, &units) != nullptr;
}

void OutlinePen::begin(std::uint32_t string) {
  close();
  string_ = string;
}

bool OutlinePen::continues(unsigned index, float line, float scale_y) const {
  if (open_.empty() || index < next_ ||
      std::fabs(line - line_) > std::fabs(scale_y)) {
    return false;
  }
  for (unsigned i = next_; i < index; i++) {
    if (RD16(string_ + i * 2u) != ' ') {
      return false;
    }
  }
  return true;
}

bool OutlinePen::add(const OutlineFont &font, unsigned index,
                     const float box[4], OutlinePlaneScale scale,
                     PromptVertexKey key, std::uint32_t color) {
  const std::uint16_t c = RD16(string_ + index * 2u);
  float units = 0.0f;
  const OutlineCell *cell = face_cell(font, c, &units);
  if (!cell) {
    close();
    return false;
  }
  const Glyph g = read_glyph(font.record, c);
  const float across = units * font.width_scale;
  const float line = box[1] + (float)g.baseline * scale.y;
  if (!continues(index, line, scale.y)) {
    close();
    line_ = line;
    travelled_ = 0.0f;
    space_ =
        outline_glyph_space(font.face) * font.caps_scale * font.width_scale;
  } else {
    travelled_ += space_ * (float)(index - next_);
  }
  Pending p{};
  p.placed.key = key;
  PromptQuad &q = p.placed.quad;
  q.y0 = line - cell->top * units * scale.y;
  q.y1 = q.y0 + cell->height * units * scale.y;
  q.x1 = cell->width * across * scale.x; /* the width until close() */
  q.u0 = cell->u0;
  q.v0 = cell->v0;
  q.u1 = cell->u1;
  q.v1 = cell->v1;
  q.color = color;
  q.codepoint = c;
  q.sheet = kOutlineTextSheet;
  p.pen = box[0] - (float)g.offset * scale.x;
  p.advance = (float)g.advance * scale.x;
  p.travelled = travelled_;
  p.left = cell->left * across;
  p.step = cell->advance * across;
  p.scale = scale.x;
  open_.push_back(p);
  travelled_ += face_step(font, c, RD16(string_ + (index + 1u) * 2u));
  next_ = index + 1u;
  return true;
}

void OutlinePen::close() {
  if (open_.empty()) {
    return;
  }
  const Pending &first = open_.front();
  const Pending &last = open_.back();
  const float face = (last.travelled + last.step) * last.scale;
  const float retail = last.pen + last.advance - first.pen;
  const float fit = face != 0.0f ? retail / face : 1.0f;
  for (Pending &p : open_) {
    PromptQuad &q = p.placed.quad;
    const float width = q.x1;
    q.x0 = first.pen + (p.travelled * fit + p.left) * p.scale;
    q.x1 = q.x0 + width;
    placed_.push_back(p.placed);
  }
  open_.clear();
}

void outline_text_report() {
  g_registry.report();
  outline_glyph_atlas_report();
}

} // namespace x2::native
