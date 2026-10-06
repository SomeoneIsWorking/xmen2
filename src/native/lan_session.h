#ifndef X2_LAN_SESSION_H
#define X2_LAN_SESSION_H

/* The C boundary of LAN play: x2::lan::Coordinator (lan_coordinator.hpp)
   and the SessionDirector it drives. */

#ifdef __cplusplus
extern "C" {
#endif

#include "control_http.h"
#include "x86rt.h"

/* The guest's input thread, once per poll. */
void x2_lan_session_poll(CPU *cpu, double now);

/* A map finished loading: `map` is the map object FUN_00484ce0 ran on. */
void x2_lan_session_map_loaded(uint32_t map, int succeeded);

/* The main menu's Join row text ("Join <host>"), or NULL when no LAN game
   is announced. Valid until the next poll. */
const char *x2_lan_session_join_label(void);

/* The `port_lan_join` retail console command: void, no arguments, RET. */
void x2_lan_join_command(CPU *cpu);

/* /lan: presence and director status; /lan?host=1 re-forms this game as a
   lobby; /lan?join=1 joins the LAN game the browser finds. */
void x2_lan_session_route(x2_socket_t fd, const char *query);

#ifdef __cplusplus
}
#endif

#endif
