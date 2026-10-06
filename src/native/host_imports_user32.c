/*
 * USER32.dll -- what this host implements of it.
 *
 * Windowing and input, on SDL (win32_sdl.c, win32_events.c).
 *
 * ONE list, three expansions: the declarations, the table, and (for a name
 * this host spells differently from the DLL) the string the binder matches.
 * A stub renamed on one side and not the other fails to link.
 */
#include "host_imports.h"
#include "host_imports_surfaces.h"

#include "x86rt.h"

#define USER32_IMPORTS(X, XN, XO)                                              \
  X(AdjustWindowRect)                                                          \
  X(ClientToScreen)                                                            \
  X(ClipCursor)                                                                \
  X(CreateWindowExA)                                                           \
  X(DefWindowProcA)                                                            \
  X(DestroyWindow)                                                             \
  X(DispatchMessageA)                                                          \
  X(EnableWindow)                                                              \
  X(GetClientRect)                                                             \
  X(GetCursorPos)                                                              \
  X(GetDC)                                                                     \
  X(GetDesktopWindow)                                                          \
  X(GetMenu)                                                                   \
  X(GetMessageA)                                                               \
  X(GetParent)                                                                 \
  X(GetSystemMetrics)                                                          \
  X(GetWindowLongA)                                                            \
  X(GetWindowRect)                                                             \
  X(IsWindow)                                                                  \
  X(LoadCursorA)                                                               \
  X(LoadIconA)                                                                 \
  X(MapVirtualKeyA)                                                            \
  X(MessageBoxA)                                                               \
  X(MoveWindow)                                                                \
  X(PeekMessageA)                                                              \
  X(PtInRect)                                                                  \
  X(RegisterClassA)                                                            \
  X(ReleaseCapture)                                                            \
  X(ReleaseDC)                                                                 \
  X(ScreenToClient)                                                            \
  X(SetCapture)                                                                \
  X(SetClassLongA)                                                             \
  X(SetCursorPos)                                                              \
  X(SetWindowLongA)                                                            \
  X(SetWindowPos)                                                              \
  X(SetWindowTextA)                                                            \
  X(ShowCursor)                                                                \
  X(ShowWindow)                                                                \
  X(TranslateMessage)                                                          \
  X(UnregisterClassA)                                                          \
  X(UpdateWindow)

#define DECL(n) void imp_USER32_##n(CPU *C);
#define DECL_N(s, n) void imp_USER32_##n(CPU *C);
#define DECL_O(o, n) void imp_USER32_##n(CPU *C);
USER32_IMPORTS(DECL, DECL_N, DECL_O)

#define ENTRY(n) {#n, 0, imp_USER32_##n},
#define ENTRY_N(s, n) {s, 0, imp_USER32_##n},
#define ENTRY_O(o, n) {"#" #o, o, imp_USER32_##n},
static const HostImport g_table[] = {USER32_IMPORTS(ENTRY, ENTRY_N, ENTRY_O)};

void host_imports_register_user32(void) {
  host_imports_register("USER32.dll", g_table,
                        sizeof g_table / sizeof g_table[0]);
}
