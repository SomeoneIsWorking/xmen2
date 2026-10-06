/*
 * Every surface, registered once, before the IAT is bound.
 */
#include "host_imports.h"
#include "host_imports_surfaces.h"

void host_imports_register_all(void) {
  host_imports_register_d3d8();
  host_imports_register_kernel32();
  host_imports_register_user32();
  host_imports_register_gdi32();
  host_imports_register_advapi32();
  host_imports_register_winmm();
  host_imports_register_msvcr71();
  host_imports_register_msvcrt();
  host_imports_register_ole32();
  host_imports_register_ws2_32();
  host_imports_register_dinput8();
  host_imports_register_dinput();
}
