/*
 * DINPUT.dll -- what this host implements of it.
 *
 * The DirectInput 7 entry point libIGDisplay asks for (dinput.c).
 *
 * ONE list, three expansions: the declarations, the table, and (for a name
 * this host spells differently from the DLL) the string the binder matches.
 * A stub renamed on one side and not the other fails to link.
 */
#include "host_imports.h"
#include "host_imports_surfaces.h"

#include "x86rt.h"

#define DINPUT_IMPORTS(X, XN, XO) X(DirectInputCreateEx)

#define DECL(n) void imp_DINPUT_##n(CPU *C);
#define DECL_N(s, n) void imp_DINPUT_##n(CPU *C);
#define DECL_O(o, n) void imp_DINPUT_##n(CPU *C);
DINPUT_IMPORTS(DECL, DECL_N, DECL_O)

#define ENTRY(n) {#n, 0, imp_DINPUT_##n},
#define ENTRY_N(s, n) {s, 0, imp_DINPUT_##n},
#define ENTRY_O(o, n) {"#" #o, o, imp_DINPUT_##n},
static const HostImport g_table[] = {DINPUT_IMPORTS(ENTRY, ENTRY_N, ENTRY_O)};

void host_imports_register_dinput(void) {
  host_imports_register("DINPUT.dll", g_table,
                        sizeof g_table / sizeof g_table[0]);
}
