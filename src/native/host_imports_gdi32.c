/*
 * GDI32.dll -- what this host implements of it.
 *
 * Two unrelated jobs -- device metrics and the DIB text path (gdi32.c).
 *
 * ONE list, three expansions: the declarations, the table, and (for a name
 * this host spells differently from the DLL) the string the binder matches.
 * A stub renamed on one side and not the other fails to link.
 */
#include "host_imports.h"
#include "host_imports_surfaces.h"

#include "x86rt.h"

#define GDI32_IMPORTS(X, XN, XO)                                               \
  X(CreateCompatibleDC)                                                        \
  X(CreateDIBSection)                                                          \
  X(DeleteDC)                                                                  \
  X(DeleteObject)                                                              \
  X(ExtTextOutA)                                                               \
  X(GetDeviceCaps)                                                             \
  X(SetBkMode)                                                                 \
  X(SetTextColor)

#define DECL(n) void imp_GDI32_##n(CPU *C);
#define DECL_N(s, n) void imp_GDI32_##n(CPU *C);
#define DECL_O(o, n) void imp_GDI32_##n(CPU *C);
GDI32_IMPORTS(DECL, DECL_N, DECL_O)

#define ENTRY(n) {#n, 0, imp_GDI32_##n},
#define ENTRY_N(s, n) {s, 0, imp_GDI32_##n},
#define ENTRY_O(o, n) {"#" #o, o, imp_GDI32_##n},
static const HostImport g_table[] = {GDI32_IMPORTS(ENTRY, ENTRY_N, ENTRY_O)};

void host_imports_register_gdi32(void) {
  host_imports_register("GDI32.dll", g_table,
                        sizeof g_table / sizeof g_table[0]);
}
