/*
 * DINPUT8.dll -- what this host implements of it.
 *
 * Resolved at run time through LoadLibrary/GetProcAddress as well as
 * by import, so it is published in both registries (dinput8.c).
 *
 * ONE list, three expansions: the declarations, the table, and (for a name
 * this host spells differently from the DLL) the string the binder matches.
 * A stub renamed on one side and not the other fails to link.
 */
#include "host_imports.h"
#include "host_imports_surfaces.h"

#include "x86rt.h"

#define DINPUT8_IMPORTS(X, XN, XO) X(DirectInput8Create)

#define DECL(n) void imp_DINPUT8_##n(CPU *C);
#define DECL_N(s, n) void imp_DINPUT8_##n(CPU *C);
#define DECL_O(o, n) void imp_DINPUT8_##n(CPU *C);
DINPUT8_IMPORTS(DECL, DECL_N, DECL_O)

#define ENTRY(n) {#n, 0, imp_DINPUT8_##n},
#define ENTRY_N(s, n) {s, 0, imp_DINPUT8_##n},
#define ENTRY_O(o, n) {"#" #o, o, imp_DINPUT8_##n},
static const HostImport g_table[] = {DINPUT8_IMPORTS(ENTRY, ENTRY_N, ENTRY_O)};

void host_imports_register_dinput8(void) {
  host_imports_register("DINPUT8.dll", g_table,
                        sizeof g_table / sizeof g_table[0]);
}
