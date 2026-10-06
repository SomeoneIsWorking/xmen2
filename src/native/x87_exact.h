/*
 * x87_exact.h -- the arithmetic native overrides repeat a guest x87 routine
 * in, and when that is exactly the guest's.
 *
 * An override repeats the routine's operation order and its 32-bit spills in
 * x87_real. Where `long double` IS the x87 ten-byte format and the host FPU
 * runs at the guest's control word, that gets the guest's bits. Elsewhere --
 * the browser, ARM64 Linux and Android, whose `long double` is a software
 * binary128, and Apple Silicon, whose is a double -- x87_real is a double:
 * the same order at 53 bits, which can round a float result differently from
 * the guest in a rare tie. Bit-exactness was given up there on purpose: the
 * guest body would otherwise run every x87 instruction emulated, and a last-
 * bit difference in a cull test or a matrix is not visible.
 *
 * Either way the override answers only where the guest body could not have
 * faulted and runs at the game's own control word; the verify modes, which
 * hold the answer to the guest body's bits, need the exact host too.
 */
#ifndef X2_X87_EXACT_H
#define X2_X87_EXACT_H

#include "x87.h"

#ifdef __cplusplus
extern "C" {
#endif

#if X86P_EXACT_LONG_DOUBLE
typedef long double x87_real;
#else
typedef double x87_real;
#endif

/* 1 when this host's long double is the x87 format and its FPU control word
   is X86P_X87_CW_INIT. */
int x87_exact_host(void);

/* 1 when the guest runs at X86P_X87_CW_INIT and has `pushes` empty registers
   below TOP -- every push a routine makes before its first pop back -- so the
   guest body could not fault and an override may answer for it. */
int x87_guest_order_applies(const X86pX87 *x87, unsigned pushes);

/* x87_guest_order_applies, on a host where the answer is the guest's bits. */
int x87_exact_for_guest(const X86pX87 *x87, unsigned pushes);

/* A verify mode's switch, `cvar`, checked against the host: 1 when it is on
   here, and a fatal refusal when it is on where x87_real is not exact, since
   it would then abort on the first last-bit difference as if it were a
   defect. */
int x87_verify_requested(const char *cvar);

#ifdef __cplusplus
}
#endif

#endif /* X2_X87_EXACT_H */
