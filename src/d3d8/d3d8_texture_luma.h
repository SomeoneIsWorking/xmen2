#pragma once

#include <cstdint>

namespace x2::d3d8 {

struct D3D8TextureLumaStats {
  int textures;
  unsigned long unreadable_uploads;
  unsigned long dropped_textures;
  double mean_luma;
};

void d3d8_texture_luma_note(uint32_t handle, uint32_t format, uint32_t width,
                            uint32_t height, const uint8_t *pixels,
                            uint32_t bytes);
void d3d8_texture_luma_report(void);
void d3d8_texture_luma_get_stats(D3D8TextureLumaStats *stats);

} // namespace x2::d3d8
