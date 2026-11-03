#include "outline_glyph_atlas.hpp"

#include "outline_kerning.hpp"
#include "x2_log.h"

#include "../ui/ui_resources.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_STROKER_H
#include FT_TRUETYPE_TABLES_H
#include FT_TRUETYPE_TAGS_H

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstring>
#include <vector>

namespace x2::native {

namespace {

constexpr std::uint16_t kFirstCode = 0x21u;
constexpr std::uint16_t kLastCode = 0xffu;
constexpr unsigned kCodes = kLastCode - kFirstCode + 1u;
/* Wide enough that level kOutlineAtlasLevels-1 still keeps cells apart. */
constexpr unsigned kGap = 16u;
/* A clear texel around each cell so linear filtering never cuts an edge. */
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

class Atlas {
public:
  const OutlineCell *cell(OutlineFace face, std::uint16_t code) {
    build();
    if (code < kFirstCode || code > kLastCode) {
      return nullptr;
    }
    const Slot &slot = slots_[(unsigned)face][code - kFirstCode];
    return slot.present ? &slot.cell : nullptr;
  }

  float kerning(OutlineFace face, std::uint16_t left, std::uint16_t right) {
    build();
    const std::vector<std::int16_t> &pairs = kerning_[(unsigned)face];
    if (pairs.empty() || left < kFirstCode || left > kLastCode ||
        right < kFirstCode || right > kLastCode) {
      return 0.0f;
    }
    return (float)pairs[(std::size_t)(left - kFirstCode) * kCodes +
                        (right - kFirstCode)] /
           64.0f;
  }

  float space(OutlineFace face) {
    build();
    return space_[(unsigned)face];
  }

  const std::uint8_t *level(unsigned index, unsigned *width, unsigned *height) {
    build();
    if (index >= levels_.size()) {
      return nullptr;
    }
    *width = std::max(1u, kOutlineAtlasW >> index);
    *height = std::max(1u, kOutlineAtlasH >> index);
    return levels_[index].data();
  }

  void report() const {
    x2_log_info("  Outline glyphs: %u glyph(s) rasterised over %u face(s); "
                "%u face file(s) unreadable, %u glyph(s) past the atlas; "
                "%u kerned pair(s)\n",
                glyphs_, faces_, unreadable_, overflow_, kerned_);
  }

private:
  struct Slot {
    bool present = false;
    OutlineCell cell{};
  };

  void build() {
    if (built_) {
      return;
    }
    built_ = true;
    FT_Library library = nullptr;
    if (FT_Init_FreeType(&library)) {
      x2_log_error("OUTLINE TEXT: FreeType could not start; text keeps the "
                   "retail glyphs.\n");
      return;
    }
    std::vector<std::uint8_t> pixels((std::size_t)kOutlineAtlasW *
                                     kOutlineAtlasH * 4u);
    for (unsigned face = 0; face < kOutlineFaces; face++) {
      rasterise_face(library, face, pixels);
    }
    FT_Done_FreeType(library);
    if (!glyphs_) {
      return;
    }
    levels_.push_back(std::move(pixels));
    for (unsigned index = 1; index < kOutlineAtlasLevels; index++) {
      levels_.push_back(downsample(levels_.back(),
                                   kOutlineAtlasW >> (index - 1),
                                   kOutlineAtlasH >> (index - 1)));
    }
    x2_log_error("OUTLINE TEXT: %u glyph(s) of %u face(s) rasterised into a "
                 "%ux%u atlas with %u level(s).\n",
                 glyphs_, faces_, kOutlineAtlasW, kOutlineAtlasH,
                 kOutlineAtlasLevels);
  }

  static float reference_height(FT_Face face, std::uint16_t reference) {
    FT_BBox box;
    FT_Glyph glyph = nullptr;
    if (FT_Load_Char(face, reference, FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP) ||
        FT_Get_Glyph(face->glyph, &glyph)) {
      return 0.0f;
    }
    FT_Glyph_Get_CBox(glyph, FT_GLYPH_BBOX_UNSCALED, &box);
    FT_Done_Glyph(glyph);
    return (float)(box.yMax - box.yMin) / 64.0f;
  }

  void rasterise_face(FT_Library library, unsigned index,
                      std::vector<std::uint8_t> &pixels) {
    const FaceFile &file = kFaceFiles[index];
    const char *path = x2::ui::ui_resource_path(file.file);
    FT_Face face = nullptr;
    FT_Stroker stroker = nullptr;
    constexpr float kProbePx = 100.0f;
    if (FT_New_Face(library, path, 0, &face) ||
        FT_Set_Char_Size(face, 0, (FT_F26Dot6)(kProbePx * 64.0f), 72, 72)) {
      x2_log_error("OUTLINE TEXT: typeface %s could not be opened; its fonts "
                   "keep the retail glyphs.\n",
                   path);
      unreadable_++;
      if (face) {
        FT_Done_Face(face);
      }
      return;
    }
    const float probe = reference_height(face, file.reference);
    if (probe <= 0.0f ||
        FT_Set_Char_Size(
            face, 0,
            (FT_F26Dot6)std::lround(kProbePx * kOutlineCapPx / probe * 64.0f),
            72, 72)) {
      x2_log_error("OUTLINE TEXT: typeface %s has no reference glyph U+%04X; "
                   "its fonts keep the retail glyphs.\n",
                   path, file.reference);
      unreadable_++;
      FT_Done_Face(face);
      return;
    }
    if (file.stroke_ratio > 0.0f && !FT_Stroker_New(library, &stroker)) {
      FT_Stroker_Set(
          stroker,
          (FT_Fixed)std::lround(kOutlineCapPx * file.stroke_ratio * 64.0f),
          FT_STROKER_LINECAP_ROUND, FT_STROKER_LINEJOIN_ROUND, 0);
    }
    faces_++;
    for (unsigned code = kFirstCode; code <= kLastCode; code++) {
      rasterise_glyph(face, stroker, index, (std::uint16_t)code, pixels);
    }
    if (stroker) {
      FT_Stroker_Done(stroker);
    }
    measure_spacing(face, index);
    FT_Done_Face(face);
  }

  /* The space advance and the kern feature's pairs, at the atlas size. */
  void measure_spacing(FT_Face face, unsigned index) {
    if (!FT_Load_Char(face, ' ', FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP)) {
      space_[index] = (float)face->glyph->advance.x / 64.0f;
    }
    FT_ULong length = 0;
    if (FT_Load_Sfnt_Table(face, TTAG_GPOS, 0, nullptr, &length) || !length) {
      return;
    }
    std::vector<std::uint8_t> table(length);
    if (FT_Load_Sfnt_Table(face, TTAG_GPOS, 0, table.data(), &length)) {
      return;
    }
    const GposKerning gpos(std::move(table));
    if (gpos.empty()) {
      return;
    }
    std::array<std::uint16_t, kCodes> glyphs{};
    for (unsigned code = kFirstCode; code <= kLastCode; code++) {
      glyphs[code - kFirstCode] = (std::uint16_t)FT_Get_Char_Index(face, code);
    }
    std::vector<std::int16_t> &pairs = kerning_[index];
    pairs.assign((std::size_t)kCodes * kCodes, 0);
    for (unsigned a = 0; a < kCodes; a++) {
      for (unsigned b = 0; b < kCodes; b++) {
        if (!glyphs[a] || !glyphs[b]) {
          continue;
        }
        const int units = gpos.adjustment(glyphs[a], glyphs[b]);
        if (units) {
          pairs[(std::size_t)a * kCodes + b] = (std::int16_t)std::clamp<long>(
              FT_MulFix(units, face->size->metrics.x_scale), INT16_MIN,
              INT16_MAX);
          kerned_++;
        }
      }
    }
  }

  void rasterise_glyph(FT_Face face, FT_Stroker stroker, unsigned index,
                       std::uint16_t code, std::vector<std::uint8_t> &pixels) {
    const FT_UInt glyph_index = FT_Get_Char_Index(face, code);
    FT_Glyph glyph = nullptr;
    if (!glyph_index ||
        FT_Load_Glyph(face, glyph_index,
                      FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP) ||
        FT_Get_Glyph(face->glyph, &glyph)) {
      return;
    }
    const float advance = (float)face->glyph->advance.x / 64.0f;
    const Bitmap fill = render(glyph, nullptr);
    const Bitmap rim = stroker ? render(glyph, stroker) : Bitmap{};
    FT_Done_Glyph(glyph);

    const Bitmap &outer = rim.width ? rim : fill;
    if (!outer.width || !outer.rows) {
      return;
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
    if (!place(width, height, &x, &y)) {
      overflow_++;
      return;
    }
    for (unsigned row = 0; row < height; row++) {
      for (unsigned col = 0; col < width; col++) {
        const int gx = left + (int)col, gy = top - (int)row;
        const float f = fill.at(gx, gy) / 255.0f;
        const float r = rim.width ? rim.at(gx, gy) / 255.0f : 0.0f;
        /* White fill over a black rim. */
        const float alpha = f + (1.0f - f) * r;
        std::uint8_t *out =
            &pixels[(((std::size_t)(y + row)) * kOutlineAtlasW + x + col) * 4u];
        const std::uint8_t white =
            alpha > 0.0f ? (std::uint8_t)std::lround(255.0f * f / alpha) : 0u;
        out[0] = white;
        out[1] = white;
        out[2] = white;
        out[3] = (std::uint8_t)std::lround(255.0f * alpha);
      }
    }
    Slot &slot = slots_[index][code - kFirstCode];
    slot.present = true;
    slot.cell.u0 = (float)x / (float)kOutlineAtlasW;
    slot.cell.u1 = (float)(x + width) / (float)kOutlineAtlasW;
    slot.cell.v1 = 1.0f - (float)y / (float)kOutlineAtlasH;
    slot.cell.v0 = 1.0f - (float)(y + height) / (float)kOutlineAtlasH;
    slot.cell.left = (float)left;
    slot.cell.top = (float)top;
    slot.cell.width = (float)width;
    slot.cell.height = (float)height;
    slot.cell.advance = advance;
    glyphs_++;
  }

  bool place(unsigned width, unsigned height, unsigned *x, unsigned *y) {
    if (pen_x_ + width > kOutlineAtlasW) {
      pen_x_ = 0;
      pen_y_ += shelf_ + kGap;
      shelf_ = 0;
    }
    if (width > kOutlineAtlasW || pen_y_ + height > kOutlineAtlasH) {
      return false;
    }
    *x = pen_x_;
    *y = pen_y_;
    pen_x_ += width + kGap;
    shelf_ = std::max(shelf_, height);
    return true;
  }

  /* Alpha-weighted 2x2 box filter, so clear texels do not darken the ink. */
  static std::vector<std::uint8_t>
  downsample(const std::vector<std::uint8_t> &source, unsigned width,
             unsigned height) {
    const unsigned w = std::max(1u, width / 2u), h = std::max(1u, height / 2u);
    std::vector<std::uint8_t> out((std::size_t)w * h * 4u);
    for (unsigned y = 0; y < h; y++) {
      for (unsigned x = 0; x < w; x++) {
        unsigned alpha = 0, color = 0;
        for (unsigned k = 0; k < 4u; k++) {
          const unsigned sx = std::min(width - 1u, x * 2u + (k & 1u));
          const unsigned sy = std::min(height - 1u, y * 2u + (k >> 1));
          const std::uint8_t *p = &source[((std::size_t)sy * width + sx) * 4u];
          alpha += p[3];
          color += (unsigned)p[0] * p[3];
        }
        std::uint8_t *o = &out[((std::size_t)y * w + x) * 4u];
        const std::uint8_t c = alpha ? (std::uint8_t)(color / alpha) : 0u;
        o[0] = c;
        o[1] = c;
        o[2] = c;
        o[3] = (std::uint8_t)(alpha / 4u);
      }
    }
    return out;
  }

  bool built_ = false;
  std::array<std::array<Slot, kCodes>, kOutlineFaces> slots_{};
  std::array<std::vector<std::int16_t>, kOutlineFaces> kerning_{};
  std::array<float, kOutlineFaces> space_{};
  std::vector<std::vector<std::uint8_t>> levels_;
  unsigned pen_x_ = 0, pen_y_ = 0, shelf_ = 0;
  unsigned glyphs_ = 0, faces_ = 0, unreadable_ = 0, overflow_ = 0;
  unsigned kerned_ = 0;
};

Atlas g_atlas;

} // namespace

const OutlineCell *outline_glyph_cell(OutlineFace face,
                                      std::uint16_t codepoint) {
  return g_atlas.cell(face, codepoint);
}

float outline_glyph_kerning(OutlineFace face, std::uint16_t left,
                            std::uint16_t right) {
  return g_atlas.kerning(face, left, right);
}

float outline_glyph_space(OutlineFace face) { return g_atlas.space(face); }

const std::uint8_t *outline_glyph_atlas_level(unsigned level, unsigned *width,
                                              unsigned *height) {
  return g_atlas.level(level, width, height);
}

void outline_glyph_atlas_report() { g_atlas.report(); }

} // namespace x2::native
