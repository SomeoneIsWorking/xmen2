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
 * So: a socket. `--control[=port]` (or X2_CONTROL=<port>) starts an HTTP/1.1
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

/* Start the server. Port from the argument, else X2_CONTROL, else off.
   REFUSES loudly (and returns 0) if a port was asked for and cannot be bound --
   a control channel that silently failed to listen is a run that ignores every
   command while looking healthy. */
int  control_start(int port);

/* Guest-state commands are drained on each keyboard poll, from the thread that
   owns guest input.
   `now` is the guest clock, and `cpu` is the guest state at that poll -- the
   one moment per frame when the guest is between operations, so a command that
   has to ASK the game something (rather than only set a host-side flag) can
   call into it safely. */
struct CPU;
void control_pump(struct CPU *cpu, double now);

/* Into the shutdown report, at zero and with its denominator. */
void control_report(void);

/* The HTTP wire helpers, shared with the endpoints that live beside their
 * instruments (the reached endpoint is x86_reached.c's, not this file's). */
void control_reply_text(int fd, int code, const char *status,
                        const char *fmt, ...);

#endif
