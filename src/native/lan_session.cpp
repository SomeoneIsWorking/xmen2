#include "lan_session.h"

#include "lan_coordinator.hpp"
#include "lan_session_director.hpp"

#include "control_query.h"
#include "guest_body.h"
#include "x86rt_native.h"

#include <array>

namespace x2::native {

namespace {

/* The script function mainMenuExit(): what the pause menu's quit dialog runs
   on Yes (text 0x7ea in the mainmenuexit handler, FUN_005f27a0). It queues
   "mainmenuexit 1". The lost-connection and "not enough players" dialogs run
   it too, but only after the session has emptied, so the coordinator counts
   it as the player's choice only while the session is still up. */
inline constexpr uint32_t kMainMenuExitScript = 0x0049fb00u;

void main_menu_exit_script(CPU *C) {
  x2::lan::coordinator().left_by_choice(*C);
  x86_guest_body(C, "XMen2.exe", kMainMenuExitScript);
}

__attribute__((constructor)) void register_main_menu_exit_script() {
  x86_register_override("XMen2.exe", kMainMenuExitScript,
                        main_menu_exit_script);
}

} // namespace

void lan_session_poll(CPU *cpu, double now) {
  x2::lan::coordinator().poll(*cpu, now);
}

void lan_session_map_loaded(uint32_t map, int succeeded) {
  x2::lan::coordinator().map_loaded(map, succeeded != 0);
}

const char *lan_session_join_label(void) {
  return x2::lan::coordinator().join_label();
}

void lan_join_command(CPU *cpu) {
  x2::lan::coordinator().join_chosen();
  /* A retail menu command is void and takes nothing: just its RET. */
  cpu->reg[kX86pEsp] += 4u;
}

void lan_session_route(x2::native::Socket fd, const char *query) {
  x2::lan::SessionDirector &director = x2::lan::session_director();
  x2::net::Friends &friends = x2::lan::coordinator().friends();
  using Role = x2::lan::SessionDirector::Role;
  std::array<char, 8> value{};
  std::array<char, 512> code{};
  std::string error;
  if (control_query_arg(query, "invite", value.data(), value.size())) {
    if (!friends.invite(error)) {
      control_reply_text(fd, 409, "Conflict", "no invite: %s\n", error.c_str());
      return;
    }
    control_reply_text(fd, 200, "OK", "%s\n", friends.status().c_str());
    return;
  }
  if (control_query_arg(query, "code", code.data(), code.size())) {
    if (!friends.paste(code.data(), error)) {
      control_reply_text(fd, 400, "Bad Request", "code not taken: %s\n",
                         error.c_str());
      return;
    }
    control_reply_text(fd, 200, "OK", "%s\n", friends.status().c_str());
    return;
  }
  const bool host =
      control_query_arg(query, "host", value.data(), value.size());
  const bool join =
      !host && control_query_arg(query, "join", value.data(), value.size());
  if (host && !x2::lan::coordinator().in_campaign()) {
    control_reply_text(fd, 409, "Conflict",
                       "no campaign is running to re-form as a lobby; a new "
                       "game is hosted from the online lobby's Host Game\n");
    return;
  }
  if (host || join) {
    if (!director.request(host ? Role::Host : Role::Join)) {
      control_reply_text(fd, 409, "Conflict",
                         "a lobby script is already running: %s\n",
                         director.status().c_str());
      return;
    }
    control_reply_text(fd, 200, "OK", "requested: %s\n",
                       host ? "this game re-forms as a LAN lobby"
                            : "this machine joins the LAN game it finds");
    return;
  }
  control_reply_text(fd, 200, "OK", "%s\ndirector: %s\n%s\n",
                     x2::lan::coordinator().status().c_str(),
                     director.status().c_str(), friends.status().c_str());
}

} // namespace x2::native
