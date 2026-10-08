#pragma once

#include <cstdint>

struct X86pCpu;

namespace x2::native {

/* Run the authoritative retail mode-3 Continue chain directly from the
   intercepted boot intro command: catalog leaf pickup, save-manager mode 3,
   header/device/file selection, state 0x1c, exact-leaf redirect. Consumes
   the cached boot request on success. Returns 0 unchanged when the retail
   manager refuses anything -- the caller falls back to the retail menu. */
int continue_boot_dispatch(struct X86pCpu *cpu);

/* Re-lay the main menu `menu` (the active CMenuMain) while it is shown, when
   an input to its plan -- the announced LAN game -- has changed. */
void main_menu_refresh(struct X86pCpu *cpu, uint32_t menu);

} // namespace x2::native
