/* Diagnostic ownership for the brightness of bytes submitted as textures. */
#include "d3d8_texture_luma.h"
#include "d3d8_types.h"

#include <stdio.h>
#include <stdlib.h>

typedef struct {
  uint32_t handle;
  uint32_t format;
  uint32_t width;
  uint32_t height;
  double luma;
} TextureLuma;

static TextureLuma g_luma[512];
static int g_luma_count;
static unsigned long g_luma_dropped;
static unsigned long g_luma_unreadable;

static double luma_565(uint16_t colour) {
  double red = ((colour >> 11) & 0x1f) / 31.0;
  double green = ((colour >> 5) & 0x3f) / 63.0;
  double blue = (colour & 0x1f) / 31.0;
  return (0.299 * red + 0.587 * green + 0.114 * blue) * 255.0;
}

static double bgra_luma(const uint8_t *pixel) {
  return 0.299 * pixel[2] + 0.587 * pixel[1] + 0.114 * pixel[0];
}

void d3d8_texture_luma_note(uint32_t handle, uint32_t format, uint32_t width,
                            uint32_t height, const uint8_t *pixels,
                            uint32_t bytes) {
  double sum = 0.0;
  uint32_t samples = 0;
  uint32_t offset;
  int index;
  if (!width || !height || !pixels || !bytes)
    return;
  switch (format) {
  case D3DFMT_R8G8B8:
    for (offset = 0; offset + 3 <= bytes; offset += 3) {
      sum += bgra_luma(pixels + offset);
      samples++;
    }
    break;
  case D3DFMT_A8R8G8B8:
  case D3DFMT_X8R8G8B8:
    for (offset = 0; offset + 4 <= bytes; offset += 4) {
      sum += bgra_luma(pixels + offset);
      samples++;
    }
    break;
  case D3DFMT_DXT1:
    for (offset = 0; offset + 8 <= bytes; offset += 8) {
      sum += (luma_565((uint16_t)(pixels[offset] | (pixels[offset + 1] << 8))) +
              luma_565(
                  (uint16_t)(pixels[offset + 2] | (pixels[offset + 3] << 8)))) *
             0.5;
      samples++;
    }
    break;
  case D3DFMT_DXT2:
  case D3DFMT_DXT3:
  case D3DFMT_DXT4:
  case D3DFMT_DXT5:
    for (offset = 0; offset + 16 <= bytes; offset += 16) {
      sum += (luma_565(
                  (uint16_t)(pixels[offset + 8] | (pixels[offset + 9] << 8))) +
              luma_565((uint16_t)(pixels[offset + 10] |
                                  (pixels[offset + 11] << 8)))) *
             0.5;
      samples++;
    }
    break;
  default:
    g_luma_unreadable++;
    return;
  }
  if (!samples)
    return;
  for (index = 0; index < g_luma_count; ++index) {
    if (g_luma[index].handle != handle)
      continue;
    g_luma[index].luma = sum / samples;
    return;
  }
  if (g_luma_count == (int)(sizeof g_luma / sizeof g_luma[0])) {
    g_luma_dropped++;
    return;
  }
  g_luma[g_luma_count].handle = handle;
  g_luma[g_luma_count].format = format;
  g_luma[g_luma_count].width = width;
  g_luma[g_luma_count].height = height;
  g_luma[g_luma_count].luma = sum / samples;
  g_luma_count++;
}

void d3d8_texture_luma_report(void) {
  double total = 0.0;
  int dark = 0;
  int index;
  int rank;
  for (index = 0; index < g_luma_count; ++index) {
    total += g_luma[index].luma;
    if (g_luma[index].luma < 8.0)
      dark++;
  }
  printf("        texture brightness: %d texture(s) measured%s, %lu upload(s) "
         "in a format this cannot read, mean luma %.1f; %d of them are "
         "effectively BLACK (mean luma < 8 of 255)\n",
         g_luma_count, g_luma_dropped ? " (TABLE FULL -- more exist)" : "",
         g_luma_unreadable, g_luma_count ? total / g_luma_count : 0.0, dark);
  if (!g_luma_count) {
    printf("          none measured -- either no texture was uploaded, or "
           "every one used a format this check cannot read. It says "
           "NOTHING about the textures.\n");
    return;
  }
  if (getenv("X2_TEXTURE_LUMA_ALL")) {
    for (index = 0; index < g_luma_count; ++index)
      printf("          handle %-4u %4ux%-4u fmt %-3u  mean luma %6.2f\n",
             g_luma[index].handle, g_luma[index].width, g_luma[index].height,
             g_luma[index].format, g_luma[index].luma);
    return;
  }
  for (rank = 0; rank < 10 && rank < g_luma_count; ++rank) {
    int darkest = -1;
    for (index = 0; index < g_luma_count; ++index) {
      if (!g_luma[index].width)
        continue;
      if (darkest < 0 || g_luma[index].luma < g_luma[darkest].luma)
        darkest = index;
    }
    if (darkest < 0)
      break;
    printf("          handle %-4u %4ux%-4u fmt %-3u  mean luma %6.2f%s\n",
           g_luma[darkest].handle, g_luma[darkest].width,
           g_luma[darkest].height, g_luma[darkest].format, g_luma[darkest].luma,
           g_luma[darkest].luma < 8.0 ? "   <- BLACK" : "");
    g_luma[darkest].width = 0;
  }
}

void d3d8_texture_luma_get_stats(D3D8TextureLumaStats *stats) {
  double total = 0.0;
  int index;
  if (!stats)
    return;
  for (index = 0; index < g_luma_count; ++index)
    total += g_luma[index].luma;
  stats->textures = g_luma_count;
  stats->unreadable_uploads = g_luma_unreadable;
  stats->dropped_textures = g_luma_dropped;
  stats->mean_luma = g_luma_count ? total / g_luma_count : 0.0;
}
