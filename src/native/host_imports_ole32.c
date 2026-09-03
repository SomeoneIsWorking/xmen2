/*
 * ole32.dll -- what this host implements of it.
 *
 * COM initialisation, which this port answers without a COM runtime.
 *
 * ONE list, three expansions: the declarations, the table, and (for a name
 * this host spells differently from the DLL) the string the binder matches.
 * A stub renamed on one side and not the other fails to link.
 */
#include "host_imports.h"
#include "host_imports_surfaces.h"

#include "x86rt.h"

#define ole32_IMPORTS(X, XN, XO)                                               \
  X(CoCreateInstance)                                                          \
  X(CoInitialize)                                                              \
  X(CoInitializeEx)                                                            \
  X(CoUninitialize)

#define DECL(n) void imp_ole32_##n(CPU *C);
#define DECL_N(s, n) void imp_ole32_##n(CPU *C);
#define DECL_O(o, n) void imp_ole32_##n(CPU *C);
ole32_IMPORTS(DECL, DECL_N, DECL_O)

#define ENTRY(n) {#n, 0, imp_ole32_##n},
#define ENTRY_N(s, n) {s, 0, imp_ole32_##n},
#define ENTRY_O(o, n) {"#" #o, o, imp_ole32_##n},
    static const HostImport g_table[] = {
        ole32_IMPORTS(ENTRY, ENTRY_N, ENTRY_O)};

void host_imports_register_ole32(void) {
  host_imports_register("ole32.dll", g_table,
                        sizeof g_table / sizeof g_table[0]);
}
