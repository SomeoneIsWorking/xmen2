/* box_cull.c -- see box_cull.h. */
#include "box_cull.h"

#include "x87.h"

#include <math.h>
#include <string.h>

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

static inline long double spilled(long double value, unsigned spills,
                                  unsigned which) {
  return (spills & which) ? (long double)(float)value : value;
}

void box_cull_extent(float out[3], const float box[6]) {
  for (unsigned axis = 0; axis < 3u; axis++) {
    out[axis] = (float)((long double)box[3u + axis] - box[axis]);
  }
}

static inline void corner_axis(float out[BOX_CULL_CORNER_FLOATS],
                               const float min[3], const float extent[3],
                               const float matrix[16], long double k,
                               unsigned axis, unsigned spills,
                               int guest_order) {
  const long double base = spilled((((long double)min[0] * matrix[axis] +
                                     (long double)min[2] * matrix[8u + axis]) +
                                    (long double)min[1] * matrix[4u + axis]) +
                                       matrix[12u + axis],
                                   spills, kSpillBase);
  const long double t0 =
      spilled((long double)extent[0] * matrix[axis], spills, kSpillTerm0);
  const long double t1 =
      spilled((long double)extent[1] * matrix[4u + axis], spills, kSpillTerm1);
  const long double t2 =
      spilled((long double)extent[2] * matrix[8u + axis], spills, kSpillTerm2);
  out[0u * 4u + axis] = (float)base;
  if (!guest_order && k == 0.0L && base != 0.0L && isfinite(t0 + t1 + t2)) {
    /* A finite term times a zero `k` is a zero, and a zero plus a nonzero
       base is the base exactly, whatever either zero's sign: each corner is
       the guest's own chain of adds without its first. */
    const long double b2 = base + t2, b1 = base + t1, b21 = b2 + t1;
    out[1u * 4u + axis] = (float)b2;
    out[2u * 4u + axis] = (float)b1;
    out[3u * 4u + axis] = (float)b21;
    out[4u * 4u + axis] = (float)(base + t0);
    out[5u * 4u + axis] = (float)(b2 + t0);
    out[6u * 4u + axis] = (float)(b1 + t0);
    out[7u * 4u + axis] = (float)(b21 + t0);
    return;
  }
  /* Corner c adds extent.z for bit 0 of c, extent.y for bit 1 and extent.x
     for bit 2; the guest multiplies the extents a corner leaves out by
     `zero` and adds that first. */
  out[1u * 4u + axis] = (float)(((t1 + t0) * k + base) + t2);
  out[2u * 4u + axis] = (float)(((t2 + t0) * k + base) + t1);
  out[3u * 4u + axis] = (float)(((t0 * k + base) + t2) + t1);
  out[4u * 4u + axis] = (float)(((t2 + t1) * k + base) + t0);
  out[5u * 4u + axis] = (float)(((t1 * k + base) + t2) + t0);
  out[6u * 4u + axis] = (float)(((t2 * k + base) + t1) + t0);
  out[7u * 4u + axis] = (float)(((base + t2) + t1) + t0);
}

static void corners(float out[BOX_CULL_CORNER_FLOATS], const float min[3],
                    const float extent[3], const float matrix[16], float zero,
                    int guest_order) {
  const long double k = zero;
  corner_axis(out, min, extent, matrix, k, 0u, kSpillsX, guest_order);
  corner_axis(out, min, extent, matrix, k, 1u, kSpillsY, guest_order);
  corner_axis(out, min, extent, matrix, k, 2u, kSpillsZW, guest_order);
  corner_axis(out, min, extent, matrix, k, 3u, kSpillsZW, guest_order);
}

void box_cull_corners(float out[BOX_CULL_CORNER_FLOATS], const float min[3],
                      const float extent[3], const float matrix[16],
                      float zero) {
  corners(out, min, extent, matrix, zero, 0);
}

void box_cull_corners_guest_order(float out[BOX_CULL_CORNER_FLOATS],
                                  const float min[3], const float extent[3],
                                  const float matrix[16], float zero) {
  corners(out, min, extent, matrix, zero, 1);
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
static inline unsigned corner_code(const float corner[4]) {
  uint32_t bits[4];
  memcpy(bits, corner, sizeof bits);
  if (!(is_finite_bits(bits[0]) & is_finite_bits(bits[1]) &
        is_finite_bits(bits[2]) & is_finite_bits(bits[3]))) {
    return box_cull_corner_code_extended(corner);
  }
  const int32_t neg_w = order_key(bits[3] ^ 0x80000000u);
  unsigned code = 0u;
  for (unsigned axis = 0; axis < 3u; axis++) {
    code |= (unsigned)(neg_w < order_key(bits[axis])) << (2u * axis);
    code |= (unsigned)(neg_w < order_key(bits[axis] ^ 0x80000000u))
            << (2u * axis + 1u);
  }
  return code;
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
  unsigned inside_all = 0x3fu;
  unsigned inside_any = 0u;
  for (unsigned c = 0; c < BOX_CULL_CORNERS; c++) {
    const unsigned code = corner_code(&corners[c * 4u]);
    inside_all &= code;
    inside_any |= code;
  }
  if (inside_any != 0x3fu) {
    return kBoxCullOutside;
  }
  return inside_all == 0x3fu ? kBoxCullInside : kBoxCullUndecided;
}
