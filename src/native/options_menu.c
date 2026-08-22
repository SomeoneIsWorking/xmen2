/* Retail Options -> RmlUi bridge.
 *
 * The shipped main menu's Options row executes the BehavEd command
 * `options_main`; XMen2.exe 0x005f4900 registers that command to 0x005f1fa0.
 * Pause/generic option entries execute `options`, registered to 0x005f1c50.
 * Both original callbacks push the retail `options` menu. The host overlay
 * used to be reachable only through F1, so the authored menu path and the
 * replacement UI were two unrelated entry points.
 *
 * These overrides replace only those two command callbacks. The retail menu
 * underneath remains active while RmlUi captures input, so closing settings
 * returns to the exact menu that opened it. No global openmenu or input action
 * is intercepted.
 */
#include "options_menu.h"

#include "settings_overlay_state.h"
#include "x86rt_native.h"

static void open_settings(CPU *C)
{
    x2_settings_overlay_show();

    /* Both retail callbacks end in plain RET and take no arguments. BehavEd
       declares them void, so there is no return value for the caller to read;
       preserve the incoming caller-save registers and pop only the return. */
    C->esp += 4u;
}

void x2_override_005f1c50(CPU *C)
{
    open_settings(C);
}

void x2_override_005f1fa0(CPU *C)
{
    open_settings(C);
}

__attribute__((constructor))
static void x2_options_menu_register_overrides(void)
{
    x86_register_override("XMen2.exe", 0x005f1c50u,
                          x2_override_005f1c50);
    x86_register_override("XMen2.exe", 0x005f1fa0u,
                          x2_override_005f1fa0);
}
