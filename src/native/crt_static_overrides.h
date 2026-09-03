#ifndef X2_CRT_STATIC_OVERRIDES_H
#define X2_CRT_STATIC_OVERRIDES_H

#include "x86rt.h"

/*
 * Native stand-ins for MSVC CRT helper routines that XMen2.exe links
 * statically -- so they are ordinary translated guest code, not import thunks,
 * and the JIT runs their x87 bodies one instruction at a time through the
 * interpreter helper. Registered as overrides on their linked addresses.
 */
void x2_crt_ftol2(CPU *C);

#endif
