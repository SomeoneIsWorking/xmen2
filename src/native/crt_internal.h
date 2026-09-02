/*
 * crt_internal.h -- the three things every CRT import stub needs.
 *
 * The stubs are split across files by subject (crt.c, crt_setjmp.c), and each
 * one reads its arguments off the guest stack and returns __cdecl the same
 * way. One definition here rather than a copy per file: two spellings of "the
 * first argument is at ESP+4" is the kind of duplication that stays correct
 * until one of them is fixed.
 */
#ifndef X2_CRT_INTERNAL_H
#define X2_CRT_INTERNAL_H

#include "x86rt.h"

/* Argument `i` of a __cdecl call, with ESP still pointing at the return
   address the caller pushed. */
#define A(i) RD32(C->esp + 4u + (uint32_t)(i) * 4u)

/* Return from a __cdecl stub: set EAX and pop the return address. The callee
   pops nothing else, which is what __cdecl means. */
static inline void ret_c(CPU *C, uint32_t eax) { C->eax = eax; C->esp += 4u; }

/* Stop, naming the import and why it cannot be served. There is no plausible
   value to return: see the abort-paths note in x86rt_native.c. */
void crt_unimpl(const char *sym, const char *why);

#endif
