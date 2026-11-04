#include "gpu_outline_text.h"

#include "../native/outline_glyph_atlas.hpp"
#include "../native/x2_log.h"
#include "gpu_device.h"
#include "gpu_viewport.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace x2::gpu {

namespace {

/* Below this cap height a glyph covers no visible pixel. */
constexpr float kCapPxMin = 0.5f;
constexpr unsigned kStretchMin = 16u, kStretchMax = 256u;

struct Pixel {
  float x, y;
};

/* The target pixel the plane point (x, 0, y) lands on; false behind the eye.
   The matrix is row-vector, as the batch's stock text uses it. */
bool to_pixel(const float m[16], const float viewport[4], float x, float y,
              Pixel *out) {
  const float cx = m[0] * x + m[8] * y + m[12];
  const float cy = m[1] * x + m[9] * y + m[13];
  const float cw = m[3] * x + m[11] * y + m[15];
  if (cw <= 1e-6f) {
    return false;
  }
  out->x = viewport[0] + (cx / cw + 1.0f) * 0.5f * viewport[2];
  out->y = viewport[1] + (1.0f - cy / cw) * 0.5f * viewport[3];
  return true;
}

class Pages {
public:
  bool upload() {
    const unsigned pages = x2::native::outline_glyph_pages();
    for (unsigned i = 0; i < pages; i++) {
      const std::uint8_t *pixels = x2::native::outline_glyph_page(i);
      unsigned first = 0, rows = 0;
      if (!textures_[i]) {
        textures_[i] =
            gpu_texture_create(x2::native::kOutlinePageW,
                               x2::native::kOutlinePageH, GPU_FMT_RGBA8, 1);
        if (!textures_[i] ||
            !gpu_texture_upload(textures_[i], 0, pixels,
                                x2::native::kOutlinePageW *
                                    x2::native::kOutlinePageH * 4u)) {
          return refuse(i);
        }
        x2::native::outline_glyph_page_take_dirty(i, &first, &rows);
        continue;
      }
      if (x2::native::outline_glyph_page_take_dirty(i, &first, &rows)) {
        const std::size_t stride = x2::native::kOutlinePageW * 4u;
        if (!gpu_texture_upload_region(textures_[i], 0, 0, first, rows,
                                       pixels + first * stride,
                                       (std::uint32_t)(rows * stride))) {
          return refuse(i);
        }
        uploads_++;
      }
    }
    return true;
  }

  GpuTexture texture(unsigned page) const {
    return page < textures_.size() ? textures_[page] : 0;
  }

  void shutdown() {
    for (GpuTexture &t : textures_) {
      if (t) {
        gpu_texture_destroy(t);
      }
      t = 0;
    }
  }

  unsigned long uploads() const { return uploads_; }

private:
  bool refuse(unsigned page) {
    if (textures_[page]) {
      gpu_texture_destroy(textures_[page]);
    }
    textures_[page] = 0;
    x2_log_error("prompt GPU: outline text page %u could not be uploaded; "
                 "its glyphs will be refused.\n",
                 page);
    return false;
  }

  std::array<GpuTexture, x2::native::kOutlinePagesMax> textures_{};
  unsigned long uploads_ = 0;
};

Pages g_pages;
unsigned long g_placed, g_unseen, g_unplaced;

} // namespace

void gpu_outline_text_frame_begin() { x2::native::outline_glyph_frame_begin(); }

bool gpu_outline_text_place(const x2::native::PromptQuad &glyph,
                            const float mvp[16], x2::native::PromptQuad *quad,
                            unsigned *page) {
  std::uint32_t target_w = 0, target_h = 0;
  GpuViewportRect rect{};
  gpu_device_presentation_size(&target_w, &target_h);
  if (!gpu_viewport_resolve(target_w, target_h, &rect)) {
    g_unseen++;
    return false;
  }
  const float viewport[4] = {rect.x, rect.y, rect.w, rect.h};
  Pixel pen{}, right{}, up{};
  if (!to_pixel(mvp, viewport, glyph.x0, glyph.y0, &pen) ||
      !to_pixel(mvp, viewport, glyph.x1, glyph.y0, &right) ||
      !to_pixel(mvp, viewport, glyph.x0, glyph.y1, &up)) {
    g_unseen++;
    return false;
  }
  /* One cap height across and up, on the target. */
  const Pixel across{right.x - pen.x, right.y - pen.y};
  const Pixel rise{up.x - pen.x, up.y - pen.y};
  const float wide = std::hypot(across.x, across.y);
  const float tall = std::hypot(rise.x, rise.y);
  if (tall < kCapPxMin || wide < kCapPxMin) {
    g_unseen++;
    return false;
  }
  const Pixel ex{across.x / wide, across.y / wide};
  const Pixel ey{rise.x / tall, rise.y / tall};
  /* The pen in the glyph's own pixel grid: whole rows, quarter columns. */
  const float pen_u = pen.x * ex.x + pen.y * ex.y;
  const float pen_v = pen.x * ey.x + pen.y * ey.y;
  const float snapped_u =
      std::floor(pen_u * (float)x2::native::kOutlinePhases + 0.5f) /
      (float)x2::native::kOutlinePhases;
  const float whole_u = std::floor(snapped_u);
  const x2::native::OutlineRasterSize size{
      (std::uint16_t)std::clamp(std::lround(tall * 4.0f), 1L,
                                (long)x2::native::kOutlineCapQuartersMax),
      (std::uint16_t)std::clamp(std::lround(wide / tall * 64.0f),
                                (long)kStretchMin, (long)kStretchMax),
      (std::uint8_t)std::lround((snapped_u - whole_u) *
                                (float)x2::native::kOutlinePhases),
  };
  const x2::native::OutlineRaster *raster = x2::native::outline_glyph_raster(
      (x2::native::OutlineFace)glyph.face, glyph.codepoint, size);
  if (!raster) {
    g_unplaced++;
    return false;
  }
  if (!raster->width || !raster->height) {
    return false;
  }
  const float du = whole_u - pen_u;
  const float dv = std::round(pen_v) - pen_v;
  const float per_u = (glyph.x1 - glyph.x0) / wide;
  const float per_v = (glyph.y1 - glyph.y0) / tall;
  *quad = glyph;
  quad->x0 = glyph.x0 + (du + (float)raster->left) * per_u;
  quad->x1 = quad->x0 + (float)raster->width * per_u;
  quad->y0 = glyph.y0 + (dv + (float)raster->top) * per_v;
  quad->y1 = quad->y0 - (float)raster->height * per_v;
  quad->u0 = (float)raster->x / (float)x2::native::kOutlinePageW;
  quad->u1 =
      (float)(raster->x + raster->width) / (float)x2::native::kOutlinePageW;
  quad->v1 = 1.0f - (float)raster->y / (float)x2::native::kOutlinePageH;
  quad->v0 = 1.0f - (float)(raster->y + raster->height) /
                        (float)x2::native::kOutlinePageH;
  *page = raster->page;
  g_placed++;
  return true;
}

bool gpu_outline_text_upload() { return g_pages.upload(); }

GpuTexture gpu_outline_text_page(unsigned page) {
  return g_pages.texture(page);
}

void gpu_outline_text_shutdown() { g_pages.shutdown(); }

void gpu_outline_text_report() {
  x2_log_info("  Outline text GPU: %lu glyph(s) placed at their drawn size, "
              "%lu too small or behind the eye, %lu without a raster; %lu "
              "page row upload(s)\n",
              g_placed, g_unseen, g_unplaced, g_pages.uploads());
}

} // namespace x2::gpu
