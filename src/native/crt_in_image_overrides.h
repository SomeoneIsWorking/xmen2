#pragma once

#include "x86rt.h"

namespace x2::native {

/*
 * Native stand-ins for MSVC CRT helper routines embedded in XMen2.exe. They
 * are ordinary guest code rather than imports, so overrides are registered by
 * linked title address.
 */
void crt_ftol2(CPU *C);
/* The same conversion in place of a direct CALL; always completes. */
int crt_ftol2_leaf(CPU *C);

} // namespace x2::native
