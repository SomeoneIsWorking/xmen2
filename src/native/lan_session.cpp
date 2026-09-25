#include "lan_session.h"

#include "lan_session_director.hpp"

extern "C" {
#include "control_query.h"
}

#include <array>

extern "C" void x2_lan_session_poll(CPU *cpu, double now) {
  x2::lan::session_director().poll(*cpu, now);
}

extern "C" void x2_lan_session_route(x2_socket_t fd, const char *query) {
  x2::lan::SessionDirector &director = x2::lan::session_director();
  using Role = x2::lan::SessionDirector::Role;
  std::array<char, 8> value{};
  const bool host =
      control_query_arg(query, "host", value.data(), value.size());
  const bool join =
      !host && control_query_arg(query, "join", value.data(), value.size());
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
  control_reply_text(fd, 200, "OK", "%s\n", director.status().c_str());
}
