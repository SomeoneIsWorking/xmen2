/* Port Settings command registration.
 *
 * XMen2.exe 0x005f4900 registers the retail menu-command table. Its authored
 * `options` and `options_main` callbacks remain retail-owned: those are game
 * UI, not spare hooks for the port.
 *
 * The derived pause XMLB instead emits `port_settings`. After the retail
 * registrar has completed, this override adds that one command through the
 * same registry vtable method the original function uses. The registry copies
 * command names, so the temporary guest-heap string can then be released.
 */
#include "options_menu.h"

#include "guest_heap.h"
#include "settings_overlay_state.h"
#include "x86rt_native.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    EXE_PREFERRED = 0x00400000u,
    COMMAND_REGISTRY_RVA = 0x0015c890u,
    REGISTER_COMMAND_VSLOT = 0x10u
};

static uint32_t g_exe;
static uint32_t g_port_settings_callback;
static int g_port_settings_registered;

void fn_XMen2_005f4900(CPU *C);

static uint32_t exe_base(void)
{
    const X86Module *module;
    if (g_exe) return g_exe;
    for (module = x86_modules(); module; module = module->next)
        if (module->preferred == EXE_PREFERRED && *module->base) {
            g_exe = *module->base;
            break;
        }
    return g_exe;
}

void x2_port_settings_command(CPU *C)
{
    x2_settings_overlay_show();
    /* BehavEd menu commands are void/no-argument callbacks ending in RET. */
    C->esp += 4u;
}

static void refuse_registration(const char *reason)
{
    fprintf(stderr, "options menu: cannot register `port_settings`: %s\n",
            reason);
    abort();
}

static void register_port_settings(const CPU *source)
{
    static const char command[] = "port_settings";
    CPU call = *source;
    uint32_t base = exe_base();
    uint32_t manager, method, name;

    if (g_port_settings_registered) return;
    if (!base) refuse_registration("XMen2.exe is not mapped");
    if (!g_port_settings_callback)
        g_port_settings_callback = x86_native_callback(
            x2_port_settings_command, "options_menu", command, NULL);
    name = guest_malloc(sizeof command);
    if (!name)
        refuse_registration("guest heap could not hold the command name");
    memcpy((void *)(uintptr_t)name, command, sizeof command);

    x86_guest_call_args(&call, base + COMMAND_REGISTRY_RVA, 0u);
    manager = call.eax;
    if (!manager) refuse_registration("the retail command registry is absent");
    method = RD32(RD32(manager) + REGISTER_COMMAND_VSLOT);
    if (!method)
        refuse_registration("the retail register-command method is absent");
    call.esp -= 4u;
    WR32(call.esp, g_port_settings_callback);
    call.esp -= 4u;
    WR32(call.esp, name);
    call.ecx = manager;
    x86_guest_call_args(&call, method, 8u);
    guest_free(name);
    if (!(call.eax & 0xffu))
        refuse_registration("the retail registry rejected the new command");
    g_port_settings_registered = 1;
}

void x2_override_005f4900(CPU *C)
{
    fn_XMen2_005f4900(C);
    register_port_settings(C);
}

__attribute__((constructor))
static void x2_options_menu_register_override(void)
{
    x86_register_override("XMen2.exe", 0x005f4900u,
                          x2_override_005f4900);
}
