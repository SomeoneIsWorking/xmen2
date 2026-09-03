/*
 * Overrides for statically-linked MSVC CRT helpers.
 *
 * XMen2.exe links _ftol2 at 0x0067217c and calls it for every
 * float/double -> int conversion the game does -- screen coordinates, timers,
 * animation and audio sample counts. It is not an import, so the JIT
 * translates its ~20-instruction body (fld/fst/fistp/fild/fsubp/fstp and the
 * truncation-correction dance that makes the fistp control-word independent)
 * and runs each of those instructions through the per-instruction interpreter
 * helper on every call. The in-game block-entry profile (issue #141) put
 * 0x0067217c..0x006721f0 at ~2.8% of guest wall time in a single leaf.
 *
 * _ftol2's observable contract is identical to _ftol: pop ST(0), truncate
 * toward zero, return the int64 in EDX:EAX, __cdecl (pop only the return
 * address). The in-body correction exists only to reach that result without
 * touching the x87 control word, where _ftol set RC=truncate first. So this
 * shares x87_crt_ftol, the implementation already used for the imported
 * MSVCR71!_ftol. Out-of-range inputs (|v| >= 2^63) are C-undefined here, the
 * same limitation x87_crt_ftol carries; the title's conversions are bounded.
 */
#include "crt_static_overrides.h"

#include "x86rt_native.h"
#include "x87crt.h"

void x2_crt_ftol2(CPU *C) { x87_crt_ftol(C); }

__attribute__((constructor)) static void crt_static_overrides_register(void) {
  x86_register_override("XMen2.exe", 0x0067217cu, x2_crt_ftol2);
}
