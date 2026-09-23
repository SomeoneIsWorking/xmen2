/* skin.c -- see skin.h. */
#include "skin.h"

/* Contraction would fuse a multiply and an add into one rounding that the
   guest's mulps/addps pair does not make. */
#pragma STDC FP_CONTRACT OFF

/* Lane `lane` of one bone's transform of `position`. Separate statements, in
   the guest's order: row0*x, + row1*y, + row2*z, + row3. */
static float transform_lane(const float *matrix, const float position[4],
                            unsigned lane) {
  float r = matrix[lane] * position[0];
  const float y = matrix[4u + lane] * position[1];
  const float z = matrix[8u + lane] * position[2];
  r = r + y;
  r = r + z;
  r = r + matrix[12u + lane];
  return r;
}

void skin_blend_vertex(float out[3], const float position[4],
                       const uint8_t *indices, const float *weights,
                       unsigned bones, const float *matrices) {
  float sum[3] = {0.0f, 0.0f, 0.0f};
  for (unsigned bone = 0; bone < bones; bone++) {
    const float *matrix = matrices + indices[bone] * SKIN_MATRIX_FLOATS;
    for (unsigned lane = 0; lane < 3u; lane++) {
      const float weighted =
          transform_lane(matrix, position, lane) * weights[bone];
      sum[lane] = sum[lane] + weighted;
    }
  }
  out[0] = sum[0];
  out[1] = sum[1];
  out[2] = sum[2];
}

void skin_rigid_vertex(float out[3], const float position[4], uint8_t index,
                       const float *matrices) {
  const float *matrix = matrices + index * SKIN_MATRIX_FLOATS;
  for (unsigned lane = 0; lane < 3u; lane++) {
    out[lane] = transform_lane(matrix, position, lane);
  }
}
