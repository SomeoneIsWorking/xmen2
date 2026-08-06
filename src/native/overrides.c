/*
 * Native overrides of recompiled guest functions.
 *
 * Declared in src/native/overrides.json, wired by tools/gen_overrides.py. Each
 * one is a `__wrap_<symbol>`; the original body stays linked as
 * `__real_<symbol>`, so an override can defer to it and the two stay diffable
 * rather than one being deleted.
 *
 * WHY OVERRIDE RATHER THAN SATISFY. The guest asks questions about a Windows
 * machine that this host is not and is not pretending to be. Some of those
 * questions have honest answers (there is no COM registry, so CoCreateInstance
 * fails -- see ole32.c). A few gate a subsystem this port replaces outright, and
 * for those the honest move is to replace the ASKING, not to fake an answer:
 * faking one means the guest proceeds to use a thing that does not exist.
 *
 * Every override announces itself once. A run in which the game skipped a
 * check must not be indistinguishable from one in which it passed.
 */
#include "x86rt.h"
#include "x86rt_native.h"

#include <stdio.h>

/* ---------------------------------------------------------------------
 * XMen2.exe 0x00617480 -- the DirectX 9.0c presence check (issue #18).
 *
 * The original reads Settings\DXChecked, and if that is not 1 it calls
 * FUN_00616f50, which CoCreateInstances the version-reporter COM object; a
 * false return produces the "DirectX not found" MessageBox and the game quits.
 *
 * Both ways of satisfying it are worse than replacing it. Setting DXChecked=1
 * makes the game cache a check that never ran and walk straight into
 * LoadLibraryA("d3d9.dll"), which this host correctly refuses. Returning a
 * fabricated S_OK hands the game an interface pointer to a vtable that does not
 * exist. The check is asking whether Microsoft's D3D is installed, and this
 * port's answer is that it does not use it -- so the question is retired.
 *
 * void __cdecl FUN_00617480(void): the caller pops nothing and the original
 * ends in a plain RET, so this leaves ESP exactly as it found it and pops only
 * the return address, exactly as the recompiled body's RET would have.
 */
void __real_fn_XMen2_00617480(CPU *C);

void __wrap_fn_XMen2_00617480(CPU *C)
{
    static int said;
    if (!said++) {
        printf("override: XMen2.exe 0x00617480, the DirectX 9.0c presence "
               "check, is REPLACED.\n"
               "  It was not passed -- it was retired. This port renders "
               "natively and does not load Microsoft's D3D,\n"
               "  so the question the check asks no longer decides anything. "
               "See src/native/overrides.json.\n");
        fflush(stdout);
    }
    /* The original returns void; the caller only continues past it. Pop the
       return address the call site pushed, as the body's RET would. */
    C->esp += 4u;
}
