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

/* A scaled corner's code: the sign of the guest's rounded -w - v and -w + v
   is exactly whether -w < v and -w < -v, as box_cull_corner_code's order
   says for floats; v here is a double holding the guest's value exactly. */
static unsigned scaled_corner_code(double neg_w, const double v[3]) {
  unsigned code = 0u;
  for (unsigned axis = 0; axis < 3u; axis++) {
    code |= (unsigned)(neg_w < v[axis]) << (2u * axis);
    code |= (unsigned)(neg_w < -v[axis]) << (2u * axis + 1u);
  }
  return code;
}

BoxCullVerdict box_cull_guard_band(const float corners[BOX_CULL_CORNER_FLOATS],
                                   BoxCullGuardBand guard) {
  uint32_t bits[BOX_CULL_CORNER_FLOATS];
  uint32_t scale_bits;
  memcpy(bits, corners, sizeof bits);
  memcpy(&scale_bits, &guard.scale, sizeof scale_bits);
  if (!is_finite_bits(scale_bits) ||
      !all_finite(bits, BOX_CULL_CORNER_FLOATS)) {
    return kBoxCullUndecided;
  }
  if (guard.scale == guard.one) {
    return kBoxCullCrossesGuardBand;
  }
  const double scale = guard.scale;
  unsigned inside_all = 0x3fu;
  for (unsigned c = 0; c < BOX_CULL_CORNERS; c++) {
    const float *corner = &corners[c * 4u];
    const float z = (float)(scale * corner[2]);
    uint32_t z_bits;
    memcpy(&z_bits, &z, sizeof z_bits);
    if (!is_finite_bits(z_bits)) {
      return kBoxCullUndecided;
    }
    const double v[3] = {scale * corner[0], scale * corner[1], z};
    inside_all &= scaled_corner_code(-(double)corner[3], v);
  }
  return inside_all == 0x3fu ? kBoxCullInsideGuardBand
                             : kBoxCullCrossesGuardBand;
}

/*
 * The bound. A guest corner is its axis's base plus up to three extent
 * terms, every product of two floats exact in either format. Its error
 * against the real sum is at most 2^-24 of each spilled value (the base and
 * three terms) and of the final float, plus the extended adds' 2^-64 steps --
 * under 5 * 2^-24 of S, the sum of the magnitudes of every product and the
 * translation. The double sum is within 10 * 2^-53 of S of the real one. So
 * the two lie within 2^-21 S of each other; 2^-20 S is used, plus 2^-140 for
 * a spill that lands among the subnormals. Beyond 2^100 a float could
 * overflow, and the bound is not claimed.
 */
#define BOX_CULL_BOUND_SCALE 0x1p-20
#define BOX_CULL_BOUND_FLOOR 0x1p-140
#define BOX_CULL_BOUND_LIMIT 0x1p100

/* Four lanes, one per matrix column: x, y, z and w of clip space. */
typedef struct BoundedLanes {
  double lane[4];
} BoundedLanes;

int box_cull_bounded_verdict(const float min[3], const float extent[3],
                             const float matrix[16], float zero,
                             BoxCullVerdict *out) {
  if (zero != 0.0f) {
    return 0;
  }
  /* Per column: the base (min through the matrix, translated), the three
     extent terms, and the bound on the corners' distance from the guest's. */
  BoundedLanes base, term[3], bound;
  double magnitude_max = 0.0;
  for (unsigned a = 0; a < 4u; a++) {
    const double p0 = (double)min[0] * matrix[a];
    const double p1 = (double)min[1] * matrix[4u + a];
    const double p2 = (double)min[2] * matrix[8u + a];
    const double translation = matrix[12u + a];
    double magnitude = fabs(p0) + fabs(p1) + fabs(p2) + fabs(translation);
    for (unsigned i = 0; i < 3u; i++) {
      term[i].lane[a] = (double)extent[i] * matrix[4u * i + a];
      magnitude += fabs(term[i].lane[a]);
    }
    base.lane[a] = ((p0 + p2) + p1) + translation;
    bound.lane[a] = magnitude * BOX_CULL_BOUND_SCALE + BOX_CULL_BOUND_FLOOR;
    magnitude_max = magnitude > magnitude_max ? magnitude : magnitude_max;
  }
  if (!(magnitude_max <= BOX_CULL_BOUND_LIMIT)) {
    return 0;
  }
  /* Over the eight corners, the least and greatest of w + v and w - v for
     each axis's v, and of w itself (lane 3 of `plus`, from w's own terms): a
     quantity linear in the corner is least with each negative term and
     greatest with each positive one. */
  double plus_low[4], plus_high[4], minus_low[4], minus_high[4], both[4];
  for (unsigned a = 0; a < 4u; a++) {
    const double v_scale = a == 3u ? 0.0 : 1.0;
    double pl = base.lane[3] + v_scale * base.lane[a];
    double ml = base.lane[3] - v_scale * base.lane[a];
    double ph = pl, mh = ml;
    for (unsigned i = 0; i < 3u; i++) {
      const double sp = term[i].lane[3] + v_scale * term[i].lane[a];
      const double sm = term[i].lane[3] - v_scale * term[i].lane[a];
      pl += sp < 0.0 ? sp : 0.0;
      ph += sp > 0.0 ? sp : 0.0;
      ml += sm < 0.0 ? sm : 0.0;
      mh += sm > 0.0 ? sm : 0.0;
    }
    plus_low[a] = pl;
    plus_high[a] = ph;
    minus_low[a] = ml;
    minus_high[a] = mh;
    both[a] = v_scale * bound.lane[a] + bound.lane[3];
  }
  /* classify's first test: every w's sign bit set -- certain when even the
     greatest w is below the bound, certainly not when it is above it. */
  if (plus_high[3] < -both[3]) {
    *out = kBoxCullOutside;
    return 1;
  }
  const int behind_unknown = !(plus_high[3] > both[3]);
  /* Per plane bit, over the corners: set in some for certain, set in every
     one for certain, clear in some for certain, set in none for certain.
     Bit 2a is -w < v, that is w + v > 0; bit 2a+1 is w - v > 0. */
  unsigned any_set = 0u, all_set = 0u, some_clear = 0u, none_set = 0u;
  for (unsigned a = 0; a < 3u; a++) {
    const unsigned lo = 1u << (2u * a), hi = lo << 1;
    any_set |= (plus_high[a] > both[a] ? lo : 0u) |
               (minus_high[a] > both[a] ? hi : 0u);
    all_set |=
        (plus_low[a] > both[a] ? lo : 0u) | (minus_low[a] > both[a] ? hi : 0u);
    some_clear |= (plus_low[a] < -both[a] ? lo : 0u) |
                  (minus_low[a] < -both[a] ? hi : 0u);
    none_set |= (plus_high[a] < -both[a] ? lo : 0u) |
                (minus_high[a] < -both[a] ? hi : 0u);
  }
  if (none_set) {
    *out = kBoxCullOutside;
    return 1;
  }
  if (behind_unknown || any_set != 0x3fu) {
    return 0;
  }
  if (all_set == 0x3fu) {
    *out = kBoxCullInside;
    return 1;
  }
  if (some_clear) {
    *out = kBoxCullUndecided;
    return 1;
  }
  return 0;
}
