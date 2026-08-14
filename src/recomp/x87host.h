#ifndef X87HOST_H
#define X87HOST_H

#include "x86rt.h"

/* Ordinary Win32 calls take scalar arguments in registers/on the stack, but
   return float and double in ST0.  The MSVC _CI* register-argument functions
   are handled separately by x87crt. */
void x87_host_begin(CPU *C);
void x87_host_end(CPU *C);

#endif
