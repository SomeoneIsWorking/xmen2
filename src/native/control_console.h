#ifndef X2_CONTROL_CONSOLE_H
#define X2_CONTROL_CONSOLE_H

/*
 * /console?command=...: run one of the game's own console commands.
 *
 * The retail front end chains itself with console commands ("openmenu
 * online", "setuphost", "mainmenuexit 1", "runscript ..."), so this is how a
 * maintainer drives one of those transitions exactly as the game's own menus
 * do, without replaying key presses through them. The command is handed over on
 * the thread that owns the guest, at its next input poll, to the console's
 * queue -- the same slot the menus use -- and runs at the console's own safe
 * point.
 *
 * The owner is x2::control::ConsoleChannel; these two functions are the C
 * boundary control.c calls through.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "control_http.h"
#include "x86rt.h"

void control_console_route(x2_socket_t fd, const char *query);

/* Called from control_pump on the guest's input thread. */
void control_console_pump(CPU *cpu);

#ifdef __cplusplus
}
#endif

#endif
