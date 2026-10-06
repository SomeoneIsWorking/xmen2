/*
 * d3d8.dll -- what this host implements of it.
 *
 * One entry point, and everything else about Direct3D 8 hangs off what it
 * returns: Direct3DCreate8 hands back a native IDirect3D8 whose vtable slots
 * are host functions (src/d3d8/), so the guest never imports another d3d8
 * symbol.
 *
 * ONE list, three expansions: the declarations, the table, and (for a name
 * this host spells differently from the DLL) the string the binder matches.
 * A stub renamed on one side and not the other fails to link.
 */
#include "host_imports.h"
#include "host_imports_surfaces.h"

#include "x86rt.h"

#define D3D8_IMPORTS(X, XN, XO) X(Direct3DCreate8)

#define DECL(n) void imp_d3d8_##n(CPU *C);
#define DECL_N(s, n) void imp_d3d8_##n(CPU *C);
#define DECL_O(o, n) void imp_d3d8_##n(CPU *C);
D3D8_IMPORTS(DECL, DECL_N, DECL_O)

#define ENTRY(n) {#n, 0, imp_d3d8_##n},
#define ENTRY_N(s, n) {s, 0, imp_d3d8_##n},
#define ENTRY_O(o, n) {"#" #o, o, imp_d3d8_##n},
static const HostImport g_table[] = {D3D8_IMPORTS(ENTRY, ENTRY_N, ENTRY_O)};

void host_imports_register_d3d8(void) {
  host_imports_register("d3d8.dll", g_table,
                        sizeof g_table / sizeof g_table[0]);
}
