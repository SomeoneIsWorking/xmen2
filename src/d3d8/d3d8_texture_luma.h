#ifndef D3D8_TEXTURE_LUMA_H
#define D3D8_TEXTURE_LUMA_H

#include <stdint.h>

typedef struct {
  int textures;
  unsigned long unreadable_uploads;
  unsigned long dropped_textures;
  double mean_luma;
} D3D8TextureLumaStats;

void d3d8_texture_luma_note(uint32_t handle, uint32_t format, uint32_t width,
                            uint32_t height, const uint8_t *pixels,
                            uint32_t bytes);
void d3d8_texture_luma_report(void);
void d3d8_texture_luma_get_stats(D3D8TextureLumaStats *stats);

#endif
