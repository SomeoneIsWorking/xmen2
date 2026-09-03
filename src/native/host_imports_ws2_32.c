/*
 * WS2_32.dll -- what this host implements of it.
 *
 * Imported entirely BY ORDINAL. Only WSAStartup (#115) and WSACleanup
 * (#116) are implemented; see ws2_32.c for why that is the answer.
 *
 * ONE list, three expansions: the declarations, the table, and (for a name
 * this host spells differently from the DLL) the string the binder matches.
 * A stub renamed on one side and not the other fails to link.
 */
#include "host_imports.h"
#include "host_imports_surfaces.h"

#include "x86rt.h"

#define WS2_32_IMPORTS(X, XN, XO)                                              \
  XO(115, _115)                                                                \
  XO(116, _116)

#define DECL(n) void imp_WS2_32_##n(CPU *C);
#define DECL_N(s, n) void imp_WS2_32_##n(CPU *C);
#define DECL_O(o, n) void imp_WS2_32_##n(CPU *C);
WS2_32_IMPORTS(DECL, DECL_N, DECL_O)

#define ENTRY(n) {#n, 0, imp_WS2_32_##n},
#define ENTRY_N(s, n) {s, 0, imp_WS2_32_##n},
#define ENTRY_O(o, n) {"#" #o, o, imp_WS2_32_##n},
static const HostImport g_table[] = {WS2_32_IMPORTS(ENTRY, ENTRY_N, ENTRY_O)};

void host_imports_register_ws2_32(void) {
  host_imports_register("WS2_32.dll", g_table,
                        sizeof g_table / sizeof g_table[0]);
}
