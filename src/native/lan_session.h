#ifndef X2_LAN_SESSION_H
#define X2_LAN_SESSION_H

/* The C boundary of x2::lan::SessionDirector (lan_session_director.hpp). */

#ifdef __cplusplus
extern "C" {
#endif

#include "control_http.h"
#include "x86rt.h"

/* The guest's input thread, once per poll. */
void x2_lan_session_poll(CPU *cpu, double now);

/* /lan: the director's status; /lan?host=1 re-forms this game as a lobby;
   /lan?join=1 joins the LAN game the browser finds. */
void x2_lan_session_route(x2_socket_t fd, const char *query);

#ifdef __cplusplus
}
#endif

#endif
