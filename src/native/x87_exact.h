/*
 * x87_exact.h -- when host `long double` arithmetic is exactly the guest's
 * x87 arithmetic.
 *
 * A native override that repeats a guest x87 routine's operation order in
 * `long double` gets the guest's bits only where `long double` IS the x87
 * ten-byte format, the host FPU runs at the control word the guest does, and
 * the guest body could not have taken a stack fault. These are the two halves
 * of that test; an override answers natively when both hold and runs the
 * guest body otherwise.
 */
#ifndef X2_X87_EXACT_H
#define X2_X87_EXACT_H

#include "x87.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 1 when this host's long double is the x87 format and its FPU control word
   is X86P_X87_CW_INIT. */
int x87_exact_host(void);

/* 1 when the guest runs at X86P_X87_CW_INIT and has `pushes` empty registers
   below TOP -- every push a routine makes before its first pop back -- and
   the host is exact. */
int x87_exact_for_guest(const X86pX87 *x87, unsigned pushes);

#ifdef __cplusplus
}
#endif

#endif /* X2_X87_EXACT_H */
