/* box_cull.c -- see box_cull.h. */
#include "box_cull.h"

#include "x87.h"

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

/* Indexed by output axis: x stays in registers, y keeps only extent.x's term
   there, z and w are spilled whole. */
static const unsigned kAxisSpills[4] = {
    0u, kSpillBase | kSpillTerm1 | kSpillTerm2, kSpillAll, kSpillAll};

int box_cull_host_exact(void) {
#if X86P_EXACT_LONG_DOUBLE && (defined(__x86_64__) || defined(__i386__))
  uint16_t control;
  __asm__ volatile("fnstcw %0" : "=m"(control));
  return control == X86P_X87_CW_INIT;
#else
  return 0;
#endif
}

static long double spilled(long double value, unsigned spills, unsigned which) {
  return (spills & which) ? (long double)(float)value : value;
}

void box_cull_corners(float out[BOX_CULL_CORNER_FLOATS], const float min[3],
                      const float extent[3], const float matrix[16],
                      float zero) {
  const long double k = zero;
  for (unsigned axis = 0; axis < 4u; axis++) {
    const unsigned spills = kAxisSpills[axis];
    const long double base =
        spilled((((long double)min[0] * matrix[axis] +
                  (long double)min[2] * matrix[8u + axis]) +
                 (long double)min[1] * matrix[4u + axis]) +
                    matrix[12u + axis],
                spills, kSpillBase);
    const long double t0 =
        spilled((long double)extent[0] * matrix[axis], spills, kSpillTerm0);
    const long double t1 = spilled((long double)extent[1] * matrix[4u + axis],
                                   spills, kSpillTerm1);
    const long double t2 = spilled((long double)extent[2] * matrix[8u + axis],
                                   spills, kSpillTerm2);
    /* Corner c adds extent.z for bit 0 of c, extent.y for bit 1 and extent.x
       for bit 2; the guest multiplies the extents a corner leaves out by
       `zero` and adds that first. */
    out[0u * 4u + axis] = (float)base;
    out[1u * 4u + axis] = (float)(((t1 + t0) * k + base) + t2);
    out[2u * 4u + axis] = (float)(((t2 + t0) * k + base) + t1);
    out[3u * 4u + axis] = (float)(((t0 * k + base) + t2) + t1);
    out[4u * 4u + axis] = (float)(((t2 + t1) * k + base) + t0);
    out[5u * 4u + axis] = (float)(((t1 * k + base) + t2) + t0);
    out[6u * 4u + axis] = (float)(((t2 * k + base) + t1) + t0);
    out[7u * 4u + axis] = (float)(((base + t2) + t1) + t0);
  }
}

/* The sign bit of the guest's 32-bit spill of `value`. */
static unsigned spilled_sign(long double value) {
  const float narrowed = (float)value;
  uint32_t bits;
  memcpy(&bits, &narrowed, sizeof bits);
  return bits >> 31;
}

/* Bits 2a and 2a+1 are the signs of -w - v and -w + v for the corner's
   coordinate v on axis a: set on the inner side of that pair of planes. */
static unsigned corner_code(const float corner[4]) {
  const long double neg_w = -(long double)corner[3];
  unsigned code = 0u;
  for (unsigned axis = 0; axis < 3u; axis++) {
    code |= spilled_sign(neg_w - corner[axis]) << (2u * axis);
    code |= spilled_sign(neg_w + corner[axis]) << (2u * axis + 1u);
  }
  return code;
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
