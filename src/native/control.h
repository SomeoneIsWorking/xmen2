/*
 * THE CONTROL CHANNEL -- drive a running game from outside it.
 *
 * Without this, the only way to ask the game a question is to decide every
 * input before launch and read the log afterwards. That forces the
 * frame-scheduled input script, and a frame-scheduled script drifts: it fires
 * its presses whether or not the game reached the state they were written for,
 * so a run that answered a dialog late sits in the menus, spends every press,
 * draws a plausible picture and reports success. Runs of that shape were read
 * as evidence twice before a file gate caught them.
 *
 * So: a socket. `--control[=port]` (or the `control.port` CVar, which is how
 * a packaged run with no environment asks) starts an HTTP/1.1
 * server on 127.0.0.1 that can press keys, read where the game thinks it is,
 * capture the frame and sample performance WHILE the run continues. Off unless
 * asked for, loopback only, and it never blocks the guest: requests are queued
 * and drained at the same poll points the input FIFO already uses.
 *
 * It is not a test harness and it is not a gate. It is a keyboard and a window
 * for a process that has neither. Guest-state commands are drained at input
 * polls; screenshots are drained at the presentation boundary because a frame
 * can keep rendering while the guest performs no input poll.
 */
#ifndef X2_CONTROL_H
#define X2_CONTROL_H

#include "platform_socket.h"
#include <stddef.h>

/* Start the server, returning the port it listens on or 0 for "nobody asked".

   Port from the argument, else the `control.port` CVar, else OFF -- and off is
   the product's state. A launch that was not asked for a channel opens no
   socket and says nothing, so a maintainer session holding a port can never
   stop a player's game from starting.

   A port that WAS asked for and cannot be bound exits the process. The run is
   about to be driven through a channel that is not there, and carrying on
   would turn that into the driving tool's timeout half a minute later. */
int control_start(int port);

/* Guest-state commands are drained on each keyboard poll, from the thread that
   owns guest input.
   `now` is the guest clock, and `cpu` is the guest state at that poll -- the
   one moment per frame when the guest is between operations, so a command that
   has to ASK the game something (rather than only set a host-side flag) can
   call into it safely. */
struct X86pCpu;
void control_pump(struct X86pCpu *cpu, double now);

/* Into the shutdown report, at zero and with its denominator. */
void control_report(void);

/* The HTTP wire helpers, shared with the endpoints that live beside their
 * instruments. */
void control_reply_text(x2_socket_t socket, int code, const char *status,
                        const char *fmt, ...);
void control_reply_json(x2_socket_t socket, int code, const char *status,
                        const char *body, size_t size);

#endif
