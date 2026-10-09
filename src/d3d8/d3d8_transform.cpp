#include "d3d8_drawcall.h"
#include "d3d8_render_states.h"

#include "gpu_matrix.h"

/* World * View on its own. D3D8's texture-coordinate generators are all
   defined in CAMERA space, so the shader needs this as well as the combined
   matrix -- and the combined one cannot be taken apart again. */
void d3d8_worldview_transform(const D3D8State *s, float out[16]) {
  using namespace x2::d3d8;
  static const float ident[16] = {1, 0, 0, 0, 0, 1, 0, 0,
                                  0, 0, 1, 0, 0, 0, 0, 1};
  const float *w =
      s->transform_set[D3DTS_WORLD] ? s->transform[D3DTS_WORLD].m : ident;
  const float *v =
      s->transform_set[D3DTS_VIEW] ? s->transform[D3DTS_VIEW].m : ident;
  gpu_matrix_multiply(w, v, out);
}

/*
 * world * view * projection, in D3D's order and D3D's row-major storage.
 *
 * The shader multiplies as `mvp * position` with a column-major mat4, which is
 * the same arithmetic as D3D's row-vector `position * M` when the matrix is
 * handed over untransposed -- so no transpose happens here, and that is a
 * deliberate non-action rather than an omission.
 */
void d3d8_combine_transform(const D3D8State *s, float out[16]) {
  using namespace x2::d3d8;
  static const float ident[16] = {1, 0, 0, 0, 0, 1, 0, 0,
                                  0, 0, 1, 0, 0, 0, 0, 1};
  const float *w =
      s->transform_set[D3DTS_WORLD] ? s->transform[D3DTS_WORLD].m : ident;
  const float *v =
      s->transform_set[D3DTS_VIEW] ? s->transform[D3DTS_VIEW].m : ident;
  const float *p = s->transform_set[D3DTS_PROJECTION]
                       ? s->transform[D3DTS_PROJECTION].m
                       : ident;
  float wv[16];
  gpu_matrix_multiply(w, v, wv);
  gpu_matrix_multiply(wv, p, out);
}
