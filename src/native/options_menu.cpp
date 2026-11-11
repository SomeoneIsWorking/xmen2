#include "x2_log.h"
/* The port's own menu commands.
 *
 * XMen2.exe 0x005f4900 registers the retail menu-command table. Its authored
 * `options` and `options_main` callbacks remain retail-owned: those are game
 * UI, not spare hooks for the port.
 *
 * The port's rows run commands of their own instead: the derived pause XMLB
 * emits `port_settings`, the main menu's LAN Join row `port_lan_join`, the
 * online lobby's friend rows `port_friend_row` (friend_code_rows.hpp), and
 * every port row of Options `port_option_next` / `port_option_prev`, which
 * step the row the menu has focused (port_option_rows.hpp).
 * After the retail registrar has completed, this override adds each through
 * the same registry vtable method the original function uses. The registry
 * copies command names, so each temporary guest-heap string is released.
 * Its node pool holds 51 commands and wraps without a word when full
 * (0x0055baa0), so the port adds as few as it can.
 */
#include "options_menu.h"

#include "friend_code_rows.hpp"
#include "port_option_rows.hpp"
#include "retail_front_end.hpp"
#include "retail_menu_model.hpp"

#include "guest_heap.h"
#include "guest_memory.h"
#include "lan_session.h"
#include "settings_overlay_state.h"
#include "x86rt_native.h"

#include "guest_body.h"
#include "guest_modules.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

enum { COMMAND_REGISTRY_RVA = 0x0015c890u, REGISTER_COMMAND_VSLOT = 0x10u };

typedef struct {
  std::string name;
  x86_override_fn run;
  const char *owner;
  void *ctx;
} PortCommand;

static int g_registered;
/* The ctx of the next and previous step commands. */
static const int kRowSteps[2] = {1, -1};

namespace x2::native {

void port_settings_command(CPU *C) {
  x2::ui::settings_overlay_show();
  /* BehavEd menu commands are void/no-argument callbacks ending in RET. */
  C->reg[kX86pEsp] += 4u;
}

/* The menu runs a row's command with that row focused. */
static void port_option_step_command(CPU *C) {
  const int direction = *static_cast<const int *>(x86_callback_ctx());
  const uint32_t menu = x2::retail::FrontEnd::active_menu_object(*C);
  const uint32_t item = menu != 0u ? RD32(menu + x2::menu::kMenuFocus) : 0u;
  const std::string name =
      item != 0u ? x2::menu::read_live_item_name(item) : std::string();
  if (const PortOptionRow *row = port_option_row_of_item(name)) {
    port_option_request(*row, direction);
  } else {
    x2_log_error("options menu: a port row command ran with \"%s\" focused, "
                 "which is no port row; nothing was changed.\n",
                 name.c_str());
  }
  C->reg[kX86pEsp] += 4u;
}

std::string port_option_command(int direction) {
  return direction < 0 ? "port_option_prev" : "port_option_next";
}

} // namespace x2::native

static std::vector<PortCommand> port_commands() {
  std::vector<PortCommand> commands = {
      {"port_settings", x2::native::port_settings_command, "options_menu",
       nullptr},
      {"port_lan_join", x2::native::lan_join_command, "lan_session", nullptr},
      {std::string(x2::native::kFriendRowCommand),
       x2::native::friend_row_command, "friend_code_rows", nullptr}};
  for (const int &direction : kRowSteps) {
    commands.push_back({x2::native::port_option_command(direction),
                        x2::native::port_option_step_command,
                        "port_option_rows", const_cast<int *>(&direction)});
  }
  return commands;
}

static void refuse_registration(const char *command, const char *reason) {
  x2_log_error("options menu: cannot register `%s`: %s\n", command, reason);
  abort();
}

static void register_command(const CPU *source, uint32_t manager,
                             const PortCommand &command) {
  const size_t bytes = command.name.size() + 1u;
  CPU call = *source;
  uint32_t method, name;
  const uint32_t callback = x86_native_callback(
      command.run, command.owner, command.name.c_str(), command.ctx);

  name = guest_malloc((uint32_t)bytes);
  if (!name)
    refuse_registration(command.name.c_str(),
                        "guest heap could not hold the command name");
  memcpy(guest_memory_pointer(name), command.name.c_str(), bytes);
  method = RD32(RD32(manager) + REGISTER_COMMAND_VSLOT);
  if (!method)
    refuse_registration(command.name.c_str(),
                        "the retail register-command method is absent");
  call.reg[kX86pEsp] -= 4u;
  WR32(call.reg[kX86pEsp], callback);
  call.reg[kX86pEsp] -= 4u;
  WR32(call.reg[kX86pEsp], name);
  call.reg[kX86pEcx] = manager;
  x86_guest_call_args(&call, method, 8u);
  guest_free(name);
  if (!(call.reg[kX86pEax] & 0xffu))
    refuse_registration(command.name.c_str(),
                        "the retail registry rejected the new command");
}

static void register_port_commands(const CPU *source) {
  CPU call = *source;
  uint32_t base = x2::native::guest_exe_base();
  uint32_t manager;

  if (g_registered)
    return;
  if (!base)
    refuse_registration("port commands", "XMen2.exe is not mapped");
  x86_guest_call_args(&call, base + COMMAND_REGISTRY_RVA, 0u);
  manager = call.reg[kX86pEax];
  if (!manager)
    refuse_registration("port commands",
                        "the retail command registry is absent");
  for (const PortCommand &command : port_commands())
    register_command(source, manager, command);
  g_registered = 1;
}

namespace x2::native {

void override_005f4900(CPU *C) {
  x86_guest_body(C, "XMen2.exe", 0x005f4900u);
  register_port_commands(C);
}

} // namespace x2::native

__attribute__((constructor)) static void
x2_options_menu_register_override(void) {
  x86_register_override("XMen2.exe", 0x005f4900u,
                        x2::native::override_005f4900);
}
