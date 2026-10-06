#include "x2_log.h"
/* The port's own menu commands.
 *
 * XMen2.exe 0x005f4900 registers the retail menu-command table. Its authored
 * `options` and `options_main` callbacks remain retail-owned: those are game
 * UI, not spare hooks for the port.
 *
 * The port's rows run commands of their own instead: the derived pause XMLB
 * emits `port_settings`, and the main menu's LAN Join row `port_lan_join`.
 * After the retail registrar has completed, this override adds each through
 * the same registry vtable method the original function uses. The registry
 * copies command names, so each temporary guest-heap string is released.
 */
#include "options_menu.h"

#include "guest_heap.h"
#include "guest_memory.h"
#include "lan_session.h"
#include "settings_overlay_state.h"
#include "x86rt_native.h"

#include "guest_body.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  EXE_PREFERRED = 0x00400000u,
  COMMAND_REGISTRY_RVA = 0x0015c890u,
  REGISTER_COMMAND_VSLOT = 0x10u
};

typedef struct {
  const char *name;
  x86_override_fn run;
  const char *owner;
} PortCommand;

static const PortCommand PORT_COMMANDS[] = {
    {"port_settings", x2_port_settings_command, "options_menu"},
    {"port_lan_join", x2_lan_join_command, "lan_session"}};

enum { PORT_COMMAND_COUNT = sizeof PORT_COMMANDS / sizeof PORT_COMMANDS[0] };

static uint32_t g_exe;
static uint32_t g_callback[PORT_COMMAND_COUNT];
static int g_registered;

static uint32_t exe_base(void) {
  const X86Module *module;
  if (g_exe)
    return g_exe;
  for (module = x86_modules(); module; module = module->next)
    if (module->preferred == EXE_PREFERRED && *module->base) {
      g_exe = *module->base;
      break;
    }
  return g_exe;
}

void x2_port_settings_command(CPU *C) {
  x2_settings_overlay_show();
  /* BehavEd menu commands are void/no-argument callbacks ending in RET. */
  C->reg[kX86pEsp] += 4u;
}

static void refuse_registration(const char *command, const char *reason) {
  x2_log_error("options menu: cannot register `%s`: %s\n", command, reason);
  abort();
}

static void register_command(const CPU *source, uint32_t manager,
                             unsigned index) {
  const PortCommand *command = &PORT_COMMANDS[index];
  const size_t bytes = strlen(command->name) + 1u;
  CPU call = *source;
  uint32_t method, name;

  if (!g_callback[index])
    g_callback[index] =
        x86_native_callback(command->run, command->owner, command->name, NULL);
  name = guest_malloc((uint32_t)bytes);
  if (!name)
    refuse_registration(command->name,
                        "guest heap could not hold the command name");
  memcpy(guest_memory_pointer(name), command->name, bytes);
  method = RD32(RD32(manager) + REGISTER_COMMAND_VSLOT);
  if (!method)
    refuse_registration(command->name,
                        "the retail register-command method is absent");
  call.reg[kX86pEsp] -= 4u;
  WR32(call.reg[kX86pEsp], g_callback[index]);
  call.reg[kX86pEsp] -= 4u;
  WR32(call.reg[kX86pEsp], name);
  call.reg[kX86pEcx] = manager;
  x86_guest_call_args(&call, method, 8u);
  guest_free(name);
  if (!(call.reg[kX86pEax] & 0xffu))
    refuse_registration(command->name,
                        "the retail registry rejected the new command");
}

static void register_port_commands(const CPU *source) {
  CPU call = *source;
  uint32_t base = exe_base();
  uint32_t manager;

  if (g_registered)
    return;
  if (!base)
    refuse_registration(PORT_COMMANDS[0].name, "XMen2.exe is not mapped");
  x86_guest_call_args(&call, base + COMMAND_REGISTRY_RVA, 0u);
  manager = call.reg[kX86pEax];
  if (!manager)
    refuse_registration(PORT_COMMANDS[0].name,
                        "the retail command registry is absent");
  for (unsigned index = 0; index < PORT_COMMAND_COUNT; index++)
    register_command(source, manager, index);
  g_registered = 1;
}

void x2_override_005f4900(CPU *C) {
  x86_guest_body(C, "XMen2.exe", 0x005f4900u);
  register_port_commands(C);
}

__attribute__((constructor)) static void
x2_options_menu_register_override(void) {
  x86_register_override("XMen2.exe", 0x005f4900u, x2_override_005f4900);
}
