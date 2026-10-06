/*
 * stdcall_import.h -- how every Win32 import stub reads and returns.
 *
 * The stubs are split across files by subsystem, and each one reads its
 * arguments off the guest stack and returns __stdcall the same way. One
 * definition here rather than a copy per file, as crt_internal.h does for
 * the __cdecl CRT stubs.
 */
#ifndef X2_STDCALL_IMPORT_H
#define X2_STDCALL_IMPORT_H

#include "x86rt.h"

/* Argument `i` of a __stdcall call, with ESP still pointing at the return
   address the caller pushed. */
#define A(i) RD32(C->reg[kX86pEsp] + 4u + (uint32_t)(i) * 4u)

/* Return from a __stdcall stub: set EAX, then pop the return address and the
   `nargs` 32-bit arguments, which the callee owns under __stdcall. */
static inline void ret_std(CPU *C, uint32_t eax, int nargs) {
  C->reg[kX86pEax] = eax;
  C->reg[kX86pEsp] += 4u + (uint32_t)nargs * 4u;
}

#endif
