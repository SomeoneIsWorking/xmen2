/*
 * ig_matrix_invert.h -- libIGMath.dll's igMatrix44f::invert (0x1001b540) as
 * host arithmetic.
 *
 * The guest computes the adjoint (0x1001b5e0) into a stack temporary, then
 * the determinant (0x1001b980), which stays in extended precision. It stores
 * the determinant to a float, and when that float's magnitude compares below
 * FLT_MIN it writes nothing and returns failure. Otherwise it scales each
 * adjoint element by the extended 1 / det and stores the product as a float.
 *
 * This repeats that in x87_real, which is exact where x87_exact_host() holds
 * (x87_exact.h). It declines (kIgInvertUndecided, `out` untouched) whenever a
 * non-finite value could raise an x87 exception the native path does not
 * record: a non-finite input, adjoint element or result.
 */
#pragma once

#include <cstdint>

namespace x2::native {

enum IgInvertVerdict {
  kIgInvertUndecided = 0, /* the guest body must run */
  kIgInvertSingular,      /* |det| < FLT_MIN: `out` untouched */
  kIgInvertInverted,      /* `out` holds the inverse */
};

/* The x87 condition codes (C3, C2, C0 of the status word) the guest's
   FCOMP of |(float)det| against FLT_MIN leaves; C1 is clear. */
inline constexpr int kIgInvertCompareGreater = 0x0000;
inline constexpr int kIgInvertCompareLess = 0x0100;
inline constexpr int kIgInvertCompareEqual = 0x4000;
inline constexpr int kIgInvertCompareMask = 0x4700;

/* `out` may be `m`: the adjoint is complete before anything is written.
   `compare_codes` is set unless the verdict is kIgInvertUndecided. */
IgInvertVerdict ig_matrix44_invert(float out[16], const float m[16],
                                   uint16_t *compare_codes);

} // namespace x2::native
