/*
 * vec3_math.hpp -- XMen2.exe's float vec3 leaves (0x005fcf70..0x005fe6c4) as
 * host arithmetic, in each body's x87 operation order (x87_exact.h).
 *
 *   0x005fcf70  affine(v, out, m), cdecl: out = v * M + T for a 3x4 matrix,
 *               rows m[0..8], translation m[9..11]
 *   0x005fe5e0  this += v      0x005fe610  this -= v      0x005fe640  this *= s
 *   0x005fe670  this . v, left in ST(0)
 *   0x005fe690  this = this x v
 *
 * Each function below is one stored component (or the dot product's value);
 * the override owns the loads and stores around them, because the bodies
 * re-read their operands from memory between stores.
 */
#pragma once

#include "x87_exact.h"

namespace x2::native {

/* out[row] of 0x005fcf70 from the operands as they are in memory then. */
float vec3_affine_row(const float v[3], const float m[12], unsigned row);

/* this[i] + v[i], this[i] - v[i] and s * this[i], each rounded once. */
float vec3_add(float self, float v);
float vec3_sub(float self, float v);
float vec3_scale(float self, float s);

/* (v2 t2 + v1 t1) + v0 t0, unrounded: the body leaves it in ST(0). */
x87_real vec3_dot(const float self[3], const float v[3]);

/* a b' - a' b for one cross-product component, with `a` and `a'` the
   register copies of `this` and `b`, `b'` read from v. */
float vec3_cross_term(float a, float b, float a2, float b2);

} // namespace x2::native
