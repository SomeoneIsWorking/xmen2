/*
 * WS2_32.dll -- what this host implements of it.
 *
 * Imported entirely BY ORDINAL: every ordinal XMen2.exe imports, on POSIX
 * sockets (ws2_32.c) and the host resolver (ws2_32_names.c).
 *
 * ONE list, three expansions: the declarations, the table, and (for a name
 * this host spells differently from the DLL) the string the binder matches.
 * A stub renamed on one side and not the other fails to link.
 */
#include "host_imports.h"
#include "host_imports_surfaces.h"

#include "x86rt.h"

#define WS2_32_IMPORTS(X, XN, XO)                                              \
  XO(2, _2)                                                                    \
  XO(3, _3)                                                                    \
  XO(4, _4)                                                                    \
  XO(6, _6)                                                                    \
  XO(8, _8)                                                                    \
  XO(9, _9)                                                                    \
  XO(10, _10)                                                                  \
  XO(11, _11)                                                                  \
  XO(12, _12)                                                                  \
  XO(14, _14)                                                                  \
  XO(15, _15)                                                                  \
  XO(16, _16)                                                                  \
  XO(17, _17)                                                                  \
  XO(18, _18)                                                                  \
  XO(19, _19)                                                                  \
  XO(20, _20)                                                                  \
  XO(21, _21)                                                                  \
  XO(22, _22)                                                                  \
  XO(23, _23)                                                                  \
  XO(52, _52)                                                                  \
  XO(57, _57)                                                                  \
  XO(111, _111)                                                                \
  XO(115, _115)                                                                \
  XO(116, _116)                                                                \
  XO(151, _151)

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
