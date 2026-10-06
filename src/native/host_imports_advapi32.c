/*
 * ADVAPI32.dll -- what this host implements of it.
 *
 * The registry surface, on the host settings store (advapi32.c).
 *
 * ONE list, three expansions: the declarations, the table, and (for a name
 * this host spells differently from the DLL) the string the binder matches.
 * A stub renamed on one side and not the other fails to link.
 */
#include "host_imports.h"
#include "host_imports_surfaces.h"

#include "x86rt.h"

#define ADVAPI32_IMPORTS(X, XN, XO)                                            \
  X(RegCloseKey)                                                               \
  X(RegCreateKeyA)                                                             \
  X(RegCreateKeyExA)                                                           \
  X(RegEnumKeyExA)                                                             \
  X(RegEnumValueA)                                                             \
  X(RegOpenKeyA)                                                               \
  X(RegOpenKeyExA)                                                             \
  X(RegQueryValueA)                                                            \
  X(RegQueryValueExA)                                                          \
  X(RegSetValueExA)

#define DECL(n) void imp_ADVAPI32_##n(CPU *C);
#define DECL_N(s, n) void imp_ADVAPI32_##n(CPU *C);
#define DECL_O(o, n) void imp_ADVAPI32_##n(CPU *C);
ADVAPI32_IMPORTS(DECL, DECL_N, DECL_O)

#define ENTRY(n) {#n, 0, imp_ADVAPI32_##n},
#define ENTRY_N(s, n) {s, 0, imp_ADVAPI32_##n},
#define ENTRY_O(o, n) {"#" #o, o, imp_ADVAPI32_##n},
static const HostImport g_table[] = {ADVAPI32_IMPORTS(ENTRY, ENTRY_N, ENTRY_O)};

void host_imports_register_advapi32(void) {
  host_imports_register("ADVAPI32.dll", g_table,
                        sizeof g_table / sizeof g_table[0]);
}
