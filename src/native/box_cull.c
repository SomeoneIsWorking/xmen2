/* box_cull.c -- see box_cull.h. */
#include "box_cull.h"

#include "x87.h"

#include <math.h>
#include <string.h>

/* The spill sets below are per-axis constants, and only inlining lets the
   compiler drop the rounding an axis does not take instead of computing both
   and selecting: these helpers are forced inline wherever the compiler can
   be told to. */
#if defined(__GNUC__) || defined(__clang__)
#define BOX_CULL_INLINE static inline __attribute__((always_inline))
#else
#define BOX_CULL_INLINE static inline
#endif

/* Which of an axis's values the guest spills through a 32-bit stack slot
   before it adds them, rather than keeping them in a register. */
enum {
  kSpillBase = 1u << 0,
  kSpillTerm0 = 1u << 1,
  kSpillTerm1 = 1u << 2,
  kSpillTerm2 = 1u << 3,
  kSpillAll = kSpillBase | kSpillTerm0 | kSpillTerm1 | kSpillTerm2,
};

/* Axis x stays in registers, y keeps only extent.x's term there, z and w are
   spilled whole. Each axis is its own call with its spill set a constant, so
   the compiler drops the rounding a value does not get instead of computing
   both and selecting with FCMOV -- which, over a table indexed in a loop, was
   the hottest part of the function. */
enum {
  kSpillsX = 0u,
  kSpillsY = kSpillBase | kSpillTerm1 | kSpillTerm2,
  kSpillsZW = kSpillAll,
};

BOX_CULL_INLINE long double spilled(long double value, unsigned spills,
                                    unsigned which) {
  return (spills & which) ? (long double)(float)value : value;
}

void box_cull_extent(float out[3], const float box[6]) {
  for (unsigned axis = 0; axis < 3u; axis++) {
    out[axis] = (float)((long double)box[3u + axis] - box[axis]);
  }
}

/* One axis's base, min through the matrix plus its translation, and the
   extents' terms, each spilled where the guest spills it. */
typedef struct AxisTerms {
  long double base, t0, t1, t2;
} AxisTerms;

BOX_CULL_INLINE AxisTerms axis_terms(const float min[3], const float extent[3],
                                     const float matrix[16], unsigned axis,
                                     unsigned spills) {
  AxisTerms t;
  t.base = spilled((((long double)min[0] * matrix[axis] +
                     (long double)min[2] * matrix[8u + axis]) +
                    (long double)min[1] * matrix[4u + axis]) +
                       matrix[12u + axis],
                   spills, kSpillBase);
  t.t0 = spilled((long double)extent[0] * matrix[axis], spills, kSpillTerm0);
  t.t1 =
      spilled((long double)extent[1] * matrix[4u + axis], spills, kSpillTerm1);
  t.t2 =
      spilled((long double)extent[2] * matrix[8u + axis], spills, kSpillTerm2);
  return t;
}

/* Corner c adds extent.z for bit 0 of c, extent.y for bit 1 and extent.x for
   bit 2; the guest multiplies the extents a corner leaves out by `zero` and
   adds that first. */
BOX_CULL_INLINE void guest_order_axis(float out[BOX_CULL_CORNER_FLOATS],
                                      unsigned axis, AxisTerms t,
                                      long double k) {
  out[0u * 4u + axis] = (float)t.base;
  out[1u * 4u + axis] = (float)(((t.t1 + t.t0) * k + t.base) + t.t2);
  out[2u * 4u + axis] = (float)(((t.t2 + t.t0) * k + t.base) + t.t1);
  out[3u * 4u + axis] = (float)(((t.t0 * k + t.base) + t.t2) + t.t1);
  out[4u * 4u + axis] = (float)(((t.t2 + t.t1) * k + t.base) + t.t0);
  out[5u * 4u + axis] = (float)(((t.t1 * k + t.base) + t.t2) + t.t0);
  out[6u * 4u + axis] = (float)(((t.t2 * k + t.base) + t.t1) + t.t0);
  out[7u * 4u + axis] = (float)(((t.base + t.t2) + t.t1) + t.t0);
}

/* A finite term times a zero `k` is a zero, and a zero plus a nonzero base
   is the base exactly, whatever either zero's sign: each corner is then the
   guest's own chain of adds without its first, and the chains share their
   prefixes. */
BOX_CULL_INLINE int zero_product_is_inert(AxisTerms t, long double k) {
  return k == 0.0L && t.base != 0.0L && isfinite(t.t0 + t.t1 + t.t2);
}

BOX_CULL_INLINE void inert_zero_axis(float out[BOX_CULL_CORNER_FLOATS],
                                     unsigned axis, AxisTerms t) {
  const long double b2 = t.base + t.t2, b1 = t.base + t.t1, b21 = b2 + t.t1;
  out[0u * 4u + axis] = (float)t.base;
  out[1u * 4u + axis] = (float)b2;
  out[2u * 4u + axis] = (float)b1;
  out[3u * 4u + axis] = (float)b21;
  out[4u * 4u + axis] = (float)(t.base + t.t0);
  out[5u * 4u + axis] = (float)(b2 + t.t0);
  out[6u * 4u + axis] = (float)(b1 + t.t0);
  out[7u * 4u + axis] = (float)(b21 + t.t0);
}

BOX_CULL_INLINE void corners_axis(float out[BOX_CULL_CORNER_FLOATS],
                                  const float min[3], const float extent[3],
                                  const float matrix[16], long double k,
                                  unsigned axis, unsigned spills) {
  const AxisTerms t = axis_terms(min, extent, matrix, axis, spills);
  if (zero_product_is_inert(t, k)) {
    inert_zero_axis(out, axis, t);
  } else {
    guest_order_axis(out, axis, t, k);
  }
}

void box_cull_corners(float out[BOX_CULL_CORNER_FLOATS], const float min[3],
                      const float extent[3], const float matrix[16],
                      float zero) {
  const long double k = zero;
  corners_axis(out, min, extent, matrix, k, 0u, kSpillsX);
  corners_axis(out, min, extent, matrix, k, 1u, kSpillsY);
  corners_axis(out, min, extent, matrix, k, 2u, kSpillsZW);
  corners_axis(out, min, extent, matrix, k, 3u, kSpillsZW);
}

void box_cull_corners_guest_order(float out[BOX_CULL_CORNER_FLOATS],
                                  const float min[3], const float extent[3],
                                  const float matrix[16], float zero) {
  const long double k = zero;
  guest_order_axis(out, 0u, axis_terms(min, extent, matrix, 0u, kSpillsX), k);
  guest_order_axis(out, 1u, axis_terms(min, extent, matrix, 1u, kSpillsY), k);
  guest_order_axis(out, 2u, axis_terms(min, extent, matrix, 2u, kSpillsZW), k);
  guest_order_axis(out, 3u, axis_terms(min, extent, matrix, 3u, kSpillsZW), k);
}

/* The sign bit of the guest's 32-bit spill of `value`. */
static unsigned spilled_sign(long double value) {
  const float narrowed = (float)value;
  uint32_t bits;
  memcpy(&bits, &narrowed, sizeof bits);
  return bits >> 31;
}

unsigned box_cull_corner_code_extended(const float corner[4]) {
  const long double neg_w = -(long double)corner[3];
  unsigned code = 0u;
  for (unsigned axis = 0; axis < 3u; axis++) {
    code |= spilled_sign(neg_w - corner[axis]) << (2u * axis);
    code |= spilled_sign(neg_w + corner[axis]) << (2u * axis + 1u);
  }
  return code;
}

/* box_cull_corner_code's order holds under round-to-nearest only: rounding
   down makes x - x come out -0. */
_Static_assert((X86P_X87_CW_INIT & 0x0c00u) == 0u,
               "the guest's control word rounds to nearest");

/* A float's bits as an integer ordered like the floats, with -0 just below
   +0: the sign magnitude of a negative float becomes a two's-complement
   value. */
static int32_t order_key(uint32_t bits) {
  return (int32_t)(bits ^ ((uint32_t)((int32_t)bits >> 31) >> 1));
}

static int is_finite_bits(uint32_t bits) {
  return (bits & 0x7fffffffu) < 0x7f800000u;
}

/* For finite floats the sign of the guest's rounded a - b is key(a) <
   key(b): rounding keeps a nonzero result's sign, floats' difference is zero
   only when exact -- it cannot underflow the extended range -- and an exact
   zero is +0 except -0 - +0, the one equal pair the order puts apart. a + b
   is a - (-b), zeros included. */
BOX_CULL_INLINE unsigned finite_corner_code(const uint32_t bits[4]) {
  const int32_t neg_w = order_key(bits[3] ^ 0x80000000u);
  unsigned code = 0u;
  for (unsigned axis = 0; axis < 3u; axis++) {
    code |= (unsigned)(neg_w < order_key(bits[axis])) << (2u * axis);
    code |= (unsigned)(neg_w < order_key(bits[axis] ^ 0x80000000u))
            << (2u * axis + 1u);
  }
  return code;
}

BOX_CULL_INLINE unsigned all_finite(const uint32_t *bits, unsigned count) {
  unsigned finite = 1u;
  for (unsigned i = 0; i < count; i++)
    finite &= (unsigned)is_finite_bits(bits[i]);
  return finite;
}

BOX_CULL_INLINE unsigned corner_code(const float corner[4]) {
  uint32_t bits[4];
  memcpy(bits, corner, sizeof bits);
  return all_finite(bits, 4u) ? finite_corner_code(bits)
                              : box_cull_corner_code_extended(corner);
}

unsigned box_cull_corner_code(const float corner[4]) {
  return corner_code(corner);
}

BoxCullVerdict box_cull_classify(const float corners[BOX_CULL_CORNER_FLOATS]) {
  /* The guest's first test reads the w words as integers. */
  uint32_t behind = 1u;
  for (unsigned c = 0; c < BOX_CULL_CORNERS; c++) {
    uint32_t bits;
    memcpy(&bits, &corners[c * 4u + 3u], sizeof bits);
    behind &= bits >> 31;
  }
  if (behind) {
    return kBoxCullOutside;
  }
  /* Boxes are finite but for a broken matrix: decide them all without a
     branch per corner, and a box with any non-finite value corner by
     corner. */
  uint32_t bits[BOX_CULL_CORNER_FLOATS];
  memcpy(bits, corners, sizeof bits);
  unsigned inside_all = 0x3fu;
  unsigned inside_any = 0u;
  if (all_finite(bits, BOX_CULL_CORNER_FLOATS)) {
    for (unsigned c = 0; c < BOX_CULL_CORNERS; c++) {
      const unsigned code = finite_corner_code(&bits[c * 4u]);
      inside_all &= code;
      inside_any |= code;
    }
  } else {
    for (unsigned c = 0; c < BOX_CULL_CORNERS; c++) {
      const unsigned code = corner_code(&corners[c * 4u]);
      inside_all &= code;
      inside_any |= code;
    }
  }
  if (inside_any != 0x3fu) {
    return kBoxCullOutside;
  }
  return inside_all == 0x3fu ? kBoxCullInside : kBoxCullUndecided;
}
