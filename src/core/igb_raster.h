#ifndef IGB_RASTER_H
#define IGB_RASTER_H

#include <stdint.h>

#include "igb.h"
#include "igb_mesh.h"

/* A single world-space textured triangle ready to project + rasterize. */
typedef struct {
    float p[3][3];   /* world-space vertices */
    float uv[3][2];  /* texture coords */
    uint8_t col[3][4]; /* vertex colors (rgba) */
    const uint8_t *tex;
    int tw, th;
} igb_raster_tri;

/* A scene expanded once into a world-space triangle list.  Textures are
 * decoded and owned here; tris reference them.  Reusable across frames. */
typedef struct {
    igb_raster_tri *tris;
    int n, cap;
    uint8_t *tex_owner[512];
    int n_tex;
} igb_raster;

igb_raster *igb_raster_build(const igb *f, const igb_scene *sc);

/* Render one frame into rgba_out (w*h*4) using a z-buffer.  view is a 4x4
 * row-major view matrix (world -> camera, camera looks down -? see docs).
 * Returns the number of triangles drawn. */
int igb_raster_frame(const igb_raster *r, const float view[16], int w, int h,
                     uint8_t *rgba_out, float *zbuf);

/* Render and write a 24-bit bottom-up BMP. */
void igb_raster_save_bmp(const igb_raster *r, const float view[16], int w, int h,
                         const char *path);

void igb_raster_free(igb_raster *r);

#endif
