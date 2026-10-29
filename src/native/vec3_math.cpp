/* vec3_math.cpp -- see vec3_math.hpp. */
#include "vec3_math.hpp"

/* Each product rounds before its sum, as on the x87; no fused multiply-add. */
#pragma STDC FP_CONTRACT OFF

namespace x2::native {

using X87 = x87_real;

float vec3_affine_row(const float v[3], const float m[12], unsigned row) {
  if (row == 0u) {
    return (float)((((X87)m[3] * v[1] + (X87)m[6] * v[2]) + (X87)v[0] * m[0]) +
                   m[9]);
  }
  return (float)((((X87)m[row] * v[0] + (X87)m[3u + row] * v[1]) +
                  (X87)m[6u + row] * v[2]) +
                 m[9u + row]);
}

float vec3_add(float self, float v) { return (float)((X87)v + self); }

float vec3_sub(float self, float v) { return (float)((X87)self - v); }

float vec3_scale(float self, float s) { return (float)((X87)s * self); }

X87 vec3_dot(const float self[3], const float v[3]) {
  return ((X87)v[2] * self[2] + (X87)v[1] * self[1]) + (X87)v[0] * self[0];
}

float vec3_cross_term(float a, float b, float a2, float b2) {
  return (float)((X87)a * b - (X87)a2 * b2);
}

} // namespace x2::native
