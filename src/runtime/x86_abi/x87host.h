#ifndef X2_RUNTIME_X87_HOST_H
#define X2_RUNTIME_X87_HOST_H

#include "x86rt.h"

/* Ordinary Win32 calls take scalar arguments in registers/on the stack, but
   return float and double in ST0.  The MSVC _CI* register-argument functions
   are handled separately by x87crt. */
void x87_host_begin(CPU *C);
void x87_host_end(CPU *C);

#endif /* X2_RUNTIME_X87_HOST_H */
