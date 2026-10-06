#include "boot_menu_transition.h"

#include "x86rt.h"
#include "x86rt_native.h"

/*
 * XMen2.exe 0x0049fb00 is the retail forced main-menu callback. It executes
 * `mainmenuexit 1`; the command handler at 0x005f27a0 takes its non-empty
 * argument branch, resets the game, and loads menu/main_back directly.
 *
 * Do not use the adjacent 0x0049fb20 callback here. It executes the
 * argument-less `mainmenuexit`, whose handler branch constructs the
 * "current game will end" confirmation. At boot that is still a valid game
 * context, so the confirmation is exactly what the live run displayed.
 */
#define MAIN_MENU_EXIT_FORCED_RVA 0x0009fb00u

int x2_boot_menu_open(const CPU *source, uint32_t exe_base) {
  CPU call;
  if (!source || !exe_base)
    return 0;
  call = *source;
  x86_guest_call_args(&call, exe_base + MAIN_MENU_EXIT_FORCED_RVA, 0u);
  return 1;
}
