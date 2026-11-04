#include "outline_glyph_atlas.hpp"

#include "outline_kerning.hpp"
#include "x2_log.h"

#include "../ui/ui_resources.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_OUTLINE_H
#include FT_STROKER_H
#include FT_TRUETYPE_TABLES_H
#include FT_TRUETYPE_TAGS_H

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstring>
#include <memory>
#include <unordered_map>
#include <vector>

namespace x2::native {

namespace {

constexpr std::uint16_t kFirstCode = 0x21u;
constexpr std::uint16_t kLastCode = 0xffu;
constexpr unsigned kCodes = kLastCode - kFirstCode + 1u;
/* A clear texel round each cell, so a filtered edge never reads a neighbour. */
constexpr int kMargin = 1;

struct FaceFile {
  const char *file;
  std::uint16_t reference;
  /* Outline width over the reference height: the retail atlases draw a
     one-texel dark rim around a 14-texel capital. */
  float stroke_ratio;
};

constexpr std::array<FaceFile, kOutlineFaces> kFaceFiles = {{
    {"Barlow-BlackItalic.ttf", 'H', 1.0f / 14.0f},
    {"Barlow-Black.ttf", 'H', 1.0f / 14.0f},
    {"DSEG7Classic-BoldItalic.ttf", '0', 0.0f},
}};

struct Bitmap {
  int left = 0, top = 0;
  unsigned width = 0, rows = 0;
  std::vector<std::uint8_t> coverage;

  std::uint8_t at(int x, int y) const {
    const int col = x - left;
    const int row = top - y;
    if (col < 0 || row < 0 || col >= (int)width || row >= (int)rows) {
      return 0;
    }
    return coverage[(std::size_t)row * width + (std::size_t)col];
  }
};

Bitmap render(FT_Glyph source, FT_Stroker stroker) {
  Bitmap out;
  FT_Glyph glyph = nullptr;
  if (FT_Glyph_Copy(source, &glyph)) {
    return out;
  }
  if ((stroker && FT_Glyph_StrokeBorder(&glyph, stroker, 0, 1)) ||
      FT_Glyph_To_Bitmap(&glyph, FT_RENDER_MODE_NORMAL, nullptr, 1)) {
    FT_Done_Glyph(glyph);
    return out;
  }
  const FT_BitmapGlyph bitmap = reinterpret_cast<FT_BitmapGlyph>(glyph);
  out.left = bitmap->left;
  out.top = bitmap->top;
  out.width = bitmap->bitmap.width;
  out.rows = bitmap->bitmap.rows;
  out.coverage.resize((std::size_t)out.width * out.rows);
  for (unsigned row = 0; row < out.rows; row++) {
    std::memcpy(&out.coverage[(std::size_t)row * out.width],
                bitmap->bitmap.buffer +
                    (std::ptrdiff_t)row * bitmap->bitmap.pitch,
                out.width);
  }
  FT_Done_Glyph(glyph);
  return out;
}

/* One atlas page, filled shelf by shelf. */
struct Page {
  std::vector<std::uint8_t> pixels = std::vector<std::uint8_t>(
      (std::size_t)kOutlinePageW * kOutlinePageH * 4u);
  unsigned pen_x = 0, pen_y = 0, shelf = 0;
  unsigned dirty_first = UINT_MAX, dirty_end = 0;

  bool place(unsigned width, unsigned height, unsigned *x, unsigned *y) {
    if (pen_x + width > kOutlinePageW) {
      pen_x = 0;
      pen_y += shelf;
      shelf = 0;
    }
    if (width > kOutlinePageW || pen_y + height > kOutlinePageH) {
      return false;
    }
    *x = pen_x;
    *y = pen_y;
    pen_x += width;
    shelf = std::max(shelf, height);
    dirty_first = std::min(dirty_first, *y);
    dirty_end = std::max(dirty_end, *y + height);
    return true;
  }

  void clear() {
    std::fill(pixels.begin(), pixels.end(), 0u);
    pen_x = pen_y = shelf = 0;
    dirty_first = 0;
    dirty_end = kOutlinePageH;
  }
};

class Cache {
public:
  ~Cache() {
    for (Face &face : faces_) {
      if (face.stroker) {
        FT_Stroker_Done(face.stroker);
      }
      if (face.face) {
        FT_Done_Face(face.face);
      }
    }
    if (library_) {
      FT_Done_FreeType(library_);
    }
  }

  bool advance(OutlineFace face, std::uint16_t code, float *out) {
    open();
    const Face &f = faces_[(unsigned)face];
    if (!f.face || code < kFirstCode || code > kLastCode ||
        !f.glyph[code - kFirstCode]) {
      return false;
    }
    *out = f.advance[code - kFirstCode];
    return true;
  }

  float kerning(OutlineFace face, std::uint16_t left, std::uint16_t right) {
    open();
    const Face &f = faces_[(unsigned)face];
    if (f.kerning.empty() || left < kFirstCode || left > kLastCode ||
        right < kFirstCode || right > kLastCode) {
      return 0.0f;
    }
    return (float)f.kerning[(std::size_t)(left - kFirstCode) * kCodes +
                            (right - kFirstCode)] /
           f.reference_units;
  }

  float space(OutlineFace face) {
    open();
    return faces_[(unsigned)face].space;
  }

  const OutlineRaster *raster(OutlineFace face, std::uint16_t code,
                              OutlineRasterSize size) {
    open();
    Face &f = faces_[(unsigned)face];
    if (!f.face || code < kFirstCode || code > kLastCode ||
        !f.glyph[code - kFirstCode] || !size.cap_quarters ||
        size.cap_quarters > kOutlineCapQuartersMax || !size.stretch_64ths ||
        size.phase >= kOutlinePhases) {
      return nullptr;
    }
    const std::uint64_t key =
        (std::uint64_t)face << 56 | (std::uint64_t)code << 40 |
        (std::uint64_t)size.cap_quarters << 24 |
        (std::uint64_t)size.stretch_64ths << 8 | size.phase;
    const auto found = rasters_.find(key);
    if (found != rasters_.end()) {
      return &found->second;
    }
    OutlineRaster out{};
    if (!rasterise(f, code, size, &out)) {
      return nullptr;
    }
    return &rasters_.emplace(key, out).first->second;
  }

  unsigned pages() const { return (unsigned)pages_.size(); }

  const std::uint8_t *page(unsigned index) const {
    return index < pages_.size() ? pages_[index]->pixels.data() : nullptr;
  }

  bool take_dirty(unsigned index, unsigned *first, unsigned *rows) {
    if (index >= pages_.size()) {
      return false;
    }
    Page &p = *pages_[index];
    if (p.dirty_first >= p.dirty_end) {
      return false;
    }
    *first = p.dirty_first;
    *rows = p.dirty_end - p.dirty_first;
    p.dirty_first = UINT_MAX;
    p.dirty_end = 0;
    return true;
  }

  void frame_begin() {
    if (!full_) {
      return;
    }
    full_ = false;
    rasters_.clear();
    for (std::unique_ptr<Page> &p : pages_) {
      p->clear();
    }
    current_ = 0;
    resets_++;
  }

  void report() const {
    x2_log_info("  Outline glyphs: %u face(s) open, %u unreadable; %zu "
                "glyph size(s) cached on %zu page(s), %lu rasterised, %lu "
                "refused for want of room, %lu reset(s); %u kerned pair(s)\n",
                opened_, unreadable_, rasters_.size(), pages_.size(),
                rasterised_, refused_, resets_, kerned_);
  }

private:
  struct Face {
    FT_Face face = nullptr;
    FT_Stroker stroker = nullptr;
    float stroke_ratio = 0.0f;
    float reference_units = 0.0f; /* the reference glyph's height */
    float space = 0.0f;
    std::array<FT_UInt, kCodes> glyph{};
    std::array<float, kCodes> advance{};
    std::vector<std::int16_t> kerning; /* font units */
    OutlineRasterSize size{};          /* the size FreeType is set to */
  };

  void open() {
    if (opened_any_) {
      return;
    }
    opened_any_ = true;
    if (FT_Init_FreeType(&library_)) {
      library_ = nullptr;
      x2_log_error("OUTLINE TEXT: FreeType could not start; text keeps the "
                   "retail glyphs.\n");
      return;
    }
    for (unsigned index = 0; index < kOutlineFaces; index++) {
      open_face(index);
    }
    x2_log_error("OUTLINE TEXT: %u of %u face(s) open; glyphs are rasterised "
                 "at their drawn size into %ux%u page(s).\n",
                 opened_, kOutlineFaces, kOutlinePageW, kOutlinePageH);
  }

  void open_face(unsigned index) {
    const FaceFile &file = kFaceFiles[index];
    const char *path = x2::ui::ui_resource_path(file.file);
    Face &f = faces_[index];
    FT_BBox box;
    FT_Glyph reference = nullptr;
    if (FT_New_Face(library_, path, 0, &f.face)) {
      f.face = nullptr;
      x2_log_error("OUTLINE TEXT: typeface %s could not be opened; its fonts "
                   "keep the retail glyphs.\n",
                   path);
      unreadable_++;
      return;
    }
    if (FT_Load_Char(f.face, file.reference, FT_LOAD_NO_SCALE) ||
        FT_Get_Glyph(f.face->glyph, &reference)) {
      x2_log_error("OUTLINE TEXT: typeface %s has no reference glyph U+%04X; "
                   "its fonts keep the retail glyphs.\n",
                   path, file.reference);
      FT_Done_Face(f.face);
      f.face = nullptr;
      unreadable_++;
      return;
    }
    FT_Glyph_Get_CBox(reference, FT_GLYPH_BBOX_UNSCALED, &box);
    FT_Done_Glyph(reference);
    f.reference_units = (float)(box.yMax - box.yMin);
    if (f.reference_units <= 0.0f) {
      FT_Done_Face(f.face);
      f.face = nullptr;
      unreadable_++;
      return;
    }
    for (unsigned code = kFirstCode; code <= kLastCode; code++) {
      const FT_UInt glyph = FT_Get_Char_Index(f.face, code);
      if (glyph && !FT_Load_Glyph(f.face, glyph, FT_LOAD_NO_SCALE)) {
        f.glyph[code - kFirstCode] = glyph;
        f.advance[code - kFirstCode] =
            (float)f.face->glyph->advance.x / f.reference_units;
      }
    }
    if (!FT_Load_Char(f.face, ' ', FT_LOAD_NO_SCALE)) {
      f.space = (float)f.face->glyph->advance.x / f.reference_units;
    }
    f.stroke_ratio = file.stroke_ratio;
    if (f.stroke_ratio > 0.0f && FT_Stroker_New(library_, &f.stroker)) {
      f.stroker = nullptr;
    }
    read_kerning(f);
    opened_++;
  }

  /* The kern feature's pairs, in font units. */
  void read_kerning(Face &f) {
    FT_ULong length = 0;
    if (FT_Load_Sfnt_Table(f.face, TTAG_GPOS, 0, nullptr, &length) || !length) {
      return;
    }
    std::vector<std::uint8_t> table(length);
    if (FT_Load_Sfnt_Table(f.face, TTAG_GPOS, 0, table.data(), &length)) {
      return;
    }
    const GposKerning gpos(std::move(table));
    if (gpos.empty()) {
      return;
    }
    f.kerning.assign((std::size_t)kCodes * kCodes, 0);
    for (unsigned a = 0; a < kCodes; a++) {
      for (unsigned b = 0; b < kCodes; b++) {
        if (!f.glyph[a] || !f.glyph[b]) {
          continue;
        }
        const int units = gpos.adjustment((std::uint16_t)f.glyph[a],
                                          (std::uint16_t)f.glyph[b]);
        if (units) {
          f.kerning[(std::size_t)a * kCodes + b] =
              (std::int16_t)std::clamp(units, INT16_MIN, INT16_MAX);
          kerned_++;
        }
      }
    }
  }

  /* FreeType's size for `size`: the reference glyph `cap` pixels tall. */
  bool set_size(Face &f, OutlineRasterSize size) {
    if (f.size.cap_quarters == size.cap_quarters &&
        f.size.stretch_64ths == size.stretch_64ths) {
      return true;
    }
    const float cap = (float)size.cap_quarters / 4.0f;
    const float em = cap * (float)f.face->units_per_EM / f.reference_units;
    const float wide = em * (float)size.stretch_64ths / 64.0f;
    if (FT_Set_Char_Size(f.face, (FT_F26Dot6)std::lround(wide * 64.0f),
                         (FT_F26Dot6)std::lround(em * 64.0f), 72, 72)) {
      f.size = OutlineRasterSize{};
      return false;
    }
    if (f.stroker) {
      FT_Stroker_Set(f.stroker,
                     (FT_Fixed)std::lround(cap * f.stroke_ratio * 64.0f),
                     FT_STROKER_LINECAP_ROUND, FT_STROKER_LINEJOIN_ROUND, 0);
    }
    f.size = size;
    f.size.phase = 0;
    return true;
  }

  bool rasterise(Face &f, std::uint16_t code, OutlineRasterSize size,
                 OutlineRaster *out) {
    FT_Glyph glyph = nullptr;
    if (!set_size(f, size) ||
        FT_Load_Glyph(f.face, f.glyph[code - kFirstCode],
                      FT_LOAD_TARGET_LIGHT | FT_LOAD_NO_BITMAP) ||
        f.face->glyph->format != FT_GLYPH_FORMAT_OUTLINE) {
      return false;
    }
    FT_Outline_Translate(&f.face->glyph->outline,
                         (FT_Pos)(size.phase * 64u / kOutlinePhases), 0);
    if (FT_Get_Glyph(f.face->glyph, &glyph)) {
      return false;
    }
    const Bitmap fill = render(glyph, nullptr);
    const Bitmap rim = f.stroker ? render(glyph, f.stroker) : Bitmap{};
    FT_Done_Glyph(glyph);
    rasterised_++;

    const Bitmap &outer = rim.width ? rim : fill;
    if (!outer.width || !outer.rows) {
      *out = OutlineRaster{};
      return true;
    }
    const int left = std::min(outer.left, fill.left) - kMargin;
    const int top = std::max(outer.top, fill.top) + kMargin;
    const int right =
        std::max(outer.left + (int)outer.width, fill.left + (int)fill.width) +
        kMargin;
    const int bottom =
        std::min(outer.top - (int)outer.rows, fill.top - (int)fill.rows) -
        kMargin;
    const unsigned width = (unsigned)(right - left);
    const unsigned height = (unsigned)(top - bottom);
    unsigned x = 0, y = 0;
    Page *page = place(width, height, &x, &y);
    if (!page) {
      refused_++;
      return false;
    }
    for (unsigned row = 0; row < height; row++) {
      for (unsigned col = 0; col < width; col++) {
        const int gx = left + (int)col, gy = top - (int)row;
        const float a = fill.at(gx, gy) / 255.0f;
        const float r = rim.width ? rim.at(gx, gy) / 255.0f : 0.0f;
        /* White fill over a black rim. */
        const float alpha = a + (1.0f - a) * r;
        std::uint8_t *texel =
            &page->pixels[(((std::size_t)(y + row)) * kOutlinePageW + x + col) *
                          4u];
        const std::uint8_t white =
            alpha > 0.0f ? (std::uint8_t)std::lround(255.0f * a / alpha) : 0u;
        texel[0] = white;
        texel[1] = white;
        texel[2] = white;
        texel[3] = (std::uint8_t)std::lround(255.0f * alpha);
      }
    }
    out->page = (std::uint16_t)current_;
    out->x = (std::uint16_t)x;
    out->y = (std::uint16_t)y;
    out->width = (std::uint16_t)width;
    out->height = (std::uint16_t)height;
    out->left = (std::int16_t)left;
    out->top = (std::int16_t)top;
    return true;
  }

  /* Room for a cell on the current page or a new one; nullptr when every
     page is full, which empties the cache at the next frame. */
  Page *place(unsigned width, unsigned height, unsigned *x, unsigned *y) {
    while (true) {
      if (current_ >= pages_.size()) {
        if (pages_.size() >= kOutlinePagesMax) {
          full_ = true;
          return nullptr;
        }
        pages_.push_back(std::make_unique<Page>());
      }
      if (pages_[current_]->place(width, height, x, y)) {
        return pages_[current_].get();
      }
      current_++;
    }
  }

  FT_Library library_ = nullptr;
  bool opened_any_ = false;
  std::array<Face, kOutlineFaces> faces_{};
  std::unordered_map<std::uint64_t, OutlineRaster> rasters_;
  std::vector<std::unique_ptr<Page>> pages_;
  std::size_t current_ = 0;
  bool full_ = false;
  unsigned opened_ = 0, unreadable_ = 0, kerned_ = 0;
  unsigned long rasterised_ = 0, refused_ = 0, resets_ = 0;
};

Cache g_cache;

} // namespace

bool outline_glyph_advance(OutlineFace face, std::uint16_t codepoint,
                           float *advance) {
  return g_cache.advance(face, codepoint, advance);
}

float outline_glyph_kerning(OutlineFace face, std::uint16_t left,
                            std::uint16_t right) {
  return g_cache.kerning(face, left, right);
}

float outline_glyph_space(OutlineFace face) { return g_cache.space(face); }

const OutlineRaster *outline_glyph_raster(OutlineFace face,
                                          std::uint16_t codepoint,
                                          OutlineRasterSize size) {
  return g_cache.raster(face, codepoint, size);
}

unsigned outline_glyph_pages() { return g_cache.pages(); }

const std::uint8_t *outline_glyph_page(unsigned page) {
  return g_cache.page(page);
}

bool outline_glyph_page_take_dirty(unsigned page, unsigned *first_row,
                                   unsigned *rows) {
  return g_cache.take_dirty(page, first_row, rows);
}

void outline_glyph_frame_begin() { g_cache.frame_begin(); }

void outline_glyph_atlas_report() { g_cache.report(); }

} // namespace x2::native
