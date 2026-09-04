#ifndef X2_CRT_IN_IMAGE_OVERRIDES_H
#define X2_CRT_IN_IMAGE_OVERRIDES_H

#include "x86rt.h"

/*
 * Native stand-ins for MSVC CRT helper routines embedded in XMen2.exe. They
 * are ordinary guest code rather than imports, so overrides are registered by
 * linked title address.
 */
void x2_crt_ftol2(CPU *C);

#endif /* X2_CRT_IN_IMAGE_OVERRIDES_H */
