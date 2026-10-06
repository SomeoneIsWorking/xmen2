/*
 * WINMM.dll -- what this host implements of it.
 *
 * The four multimedia-timer entry points libCriMovie imports (winmm.c).
 *
 * ONE list, three expansions: the declarations, the table, and (for a name
 * this host spells differently from the DLL) the string the binder matches.
 * A stub renamed on one side and not the other fails to link.
 */
#include "host_imports.h"
#include "host_imports_surfaces.h"

#include "x86rt.h"

#define WINMM_IMPORTS(X, XN, XO)                                               \
  X(timeBeginPeriod)                                                           \
  X(timeEndPeriod)                                                             \
  X(timeKillEvent)                                                             \
  X(timeSetEvent)

#define DECL(n) void imp_WINMM_##n(CPU *C);
#define DECL_N(s, n) void imp_WINMM_##n(CPU *C);
#define DECL_O(o, n) void imp_WINMM_##n(CPU *C);
WINMM_IMPORTS(DECL, DECL_N, DECL_O)

#define ENTRY(n) {#n, 0, imp_WINMM_##n},
#define ENTRY_N(s, n) {s, 0, imp_WINMM_##n},
#define ENTRY_O(o, n) {"#" #o, o, imp_WINMM_##n},
static const HostImport g_table[] = {WINMM_IMPORTS(ENTRY, ENTRY_N, ENTRY_O)};

void host_imports_register_winmm(void) {
  host_imports_register("WINMM.dll", g_table,
                        sizeof g_table / sizeof g_table[0]);
}
