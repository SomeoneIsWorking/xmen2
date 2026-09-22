/*
 * The control channel's routes that DRIVE the game's input.
 *
 * A key, a pad button, a contact on the screen, and which player owns a pad:
 * four ways of answering "press this" from outside the process, kept together
 * because they are one responsibility and kept apart from the routes that
 * only READ the run.
 *
 * None of them touches guest state. The guest is single-threaded under a
 * cooperative scheduler, and reaching into its input table from the server
 * thread is exactly the race nobody can reproduce, so each route parses a
 * request, hands it to the thread that owns guest input through
 * control_command_bridge.h, and reports what came back.
 */
#include "control_input_route.h"

#include "../input/touch_runtime.h"
#include "control_command_bridge.h"
#include "control_http.h"
#include "control_query.h"
#include "dinput_pad.h"
#include "gpu_device.h"

#include <stdlib.h>
#include <string.h>

enum { REASON_BYTES = 192 };

/*
 * One shape of answer for every route here.
 *
 * A timeout and a refusal are different facts -- "the run never got to the
 * point of reading this" against "it was read and rejected" -- and a reader
 * chasing "nothing happened" needs them apart. Saying so once means no route
 * can drift into reporting one as the other.
 */
static int delivered(x2_socket_t fd, int outcome, const char *reason,
                     const char *timeout_text) {
  if (outcome < 0) {
    control_reply_text(fd, 504, "Gateway Timeout", "%s", timeout_text);
    return 0;
  }
  if (!outcome) {
    control_reply_text(fd, 409, "Conflict", "%s\n", reason);
    return 0;
  }
  return 1;
}

void control_route_key(x2_socket_t fd, const char *query) {
  char name[32] = "", hold[16] = "", reason[REASON_BYTES] = "";
  double held;
  int outcome;

  if (!control_query_arg(query, "name", name, sizeof name) || !name[0]) {
    control_reply_text(
        fd, 400, "Bad Request",
        "no key named. Use /key?name=Return[&hold=0.3].\n"
        "Names are SDL scancode names: Return, Escape, Up, Down,\n"
        "Left, Right, Space, A, 1, F1 ...\n");
    return;
  }
  held = control_query_arg(query, "hold", hold, sizeof hold) ? atof(hold) : 0.0;
  outcome = control_command_key(name, held, reason, sizeof reason);
  if (!delivered(fd, outcome, reason,
                 "the guest did not poll its keyboard within 5s, so the key "
                 "was NOT pressed.\nThat is a statement about the RUN, not "
                 "about this channel: the game is stuck, still loading, or "
                 "has not reached its input loop.\n")) {
    return;
  }
  control_reply_text(fd, 200, "OK", "pressed \"%s\" for %.2fs at frame %lu\n",
                     name, held > 0.0 ? held : 0.30, gpu_frames_presented());
}

void control_route_pad(x2_socket_t fd, const char *query) {
  char what[32] = "", hold[16] = "", value[16] = "", reason[REASON_BYTES] = "";
  int outcome;

  if (!control_query_arg(query, "button", what, sizeof what) &&
      !control_query_arg(query, "axis", what, sizeof what)) {
    control_reply_text(
        fd, 400, "Bad Request",
        "no button or axis named.\n"
        "  /pad?button=a[&hold=0.3]\n"
        "  /pad?axis=leftx&value=-1[&hold=0.5]\n"
        "Buttons: a b x y back start leftstick rightstick "
        "leftshoulder rightshoulder\n"
        "Axes: leftx lefty rightx righty lefttrigger righttrigger, "
        "value -1..1\n");
    return;
  }
  outcome = control_command_pad(
      what,
      control_query_arg(query, "value", value, sizeof value) ? atof(value)
                                                             : 1.0,
      control_query_arg(query, "hold", hold, sizeof hold) ? atof(hold) : 0.0,
      reason, sizeof reason);
  if (!delivered(fd, outcome, reason,
                 "the guest did not poll within 5s, so the pad was NOT "
                 "set.\n")) {
    return;
  }
  /* The reason carries the pad's own read-back: "the set was accepted" and
     "the reader can see it" are different layers, and one browser run lost a
     press between them 109,780 times without either saying so. */
  control_reply_text(fd, 200, "OK", "pad \"%s\" set at frame %lu -- %s\n", what,
                     gpu_frames_presented(), reason);
}

/*
 * PRESS THE SCREEN, WHERE A FINGER WOULD.
 *
 * A host with no touchscreen has no other way to reach the on-screen controls
 * or, on the screens that draw none, the retail GUI that a tap moves. The
 * alternative is deciding every contact before the run starts and reading the
 * log afterwards, and a script that drifts answers whatever screen it landed
 * on: two such runs were read as evidence before a file gate caught them.
 */
void control_route_touch(x2_socket_t fd, const char *query) {
  static const char *const kPhases[] = {"down", "motion", "up", "cancel"};
  char x[16] = "", y[16] = "", phase[16] = "", reason[REASON_BYTES] = "";
  double at_x, at_y;
  unsigned index;
  int outcome;

  if (!control_query_arg(query, "x", x, sizeof x) ||
      !control_query_arg(query, "y", y, sizeof y)) {
    control_reply_text(fd, 400, "Bad Request",
                       "no contact position.\n"
                       "  /touch?x=0.5&y=0.9[&phase=down|motion|up|cancel]\n"
                       "x and y are fractions of the window, 0..1. The "
                       "default is a whole tap: press then release.\n");
    return;
  }
  at_x = atof(x);
  at_y = atof(y);
  if (at_x < 0.0 || at_x > 1.0 || at_y < 0.0 || at_y > 1.0) {
    control_reply_text(fd, 400, "Bad Request",
                       "x=%s y=%s is outside the window; both are fractions "
                       "of it, 0..1\n",
                       x, y);
    return;
  }

  if (!control_query_arg(query, "phase", phase, sizeof phase)) {
    /* A tap is two events. A caller that sent only the press would leave a
       finger down on the screen for the rest of the run, so the default sends
       both and says which half failed if one does. */
    outcome = control_command_touch(at_x, at_y, X2_TOUCH_PHASE_DOWN, reason,
                                    sizeof reason);
    if (!delivered(fd, outcome, reason,
                   "the guest did not pump within 5s, so the press was NOT "
                   "delivered\n")) {
      return;
    }
    outcome = control_command_touch(at_x, at_y, X2_TOUCH_PHASE_UP, reason,
                                    sizeof reason);
    if (outcome <= 0) {
      control_reply_text(fd, 409, "Conflict",
                         "the press was routed but the RELEASE was not, so a "
                         "finger is still down: %s\n",
                         outcome < 0 ? "the guest stopped pumping" : reason);
      return;
    }
    control_reply_text(fd, 200, "OK",
                       "tapped %s,%s at frame %lu -- press and release "
                       "routed\n",
                       x, y, gpu_frames_presented());
    return;
  }

  for (index = 0; index < sizeof kPhases / sizeof kPhases[0]; index++) {
    if (!strcmp(phase, kPhases[index])) {
      break;
    }
  }
  if (index >= sizeof kPhases / sizeof kPhases[0]) {
    control_reply_text(fd, 400, "Bad Request",
                       "no such phase \"%s\"; use down, motion, up or cancel\n",
                       phase);
    return;
  }
  outcome =
      control_command_touch(at_x, at_y, (int)index, reason, sizeof reason);
  if (!delivered(fd, outcome, reason,
                 "the guest did not pump within 5s, so the contact was NOT "
                 "delivered\n")) {
    return;
  }
  control_reply_text(fd, 200, "OK", "%s at %s,%s, frame %lu -- %s\n", phase, x,
                     y, gpu_frames_presented(), reason);
}

void control_route_assignment(x2_socket_t fd, const char *query) {
  char player[8] = "", pad[8] = "", clear[8] = "", reason[REASON_BYTES] = "";
  int player_number, pad_number, outcome;
  double target;

  if (!control_query_arg(query, "player", player, sizeof player) ||
      !bounded_number(player, 1, 4, &player_number)) {
    control_reply_text(fd, 400, "Bad Request",
                       "use /assignment?player=1..4&pad=N or &clear=1\n");
    return;
  }
  if (control_query_arg(query, "clear", clear, sizeof clear) && atoi(clear)) {
    target = -1.0;
  } else if (control_query_arg(query, "pad", pad, sizeof pad) &&
             bounded_number(pad, 0, DINPUT_PAD_MAX - 1, &pad_number)) {
    target = (double)pad_number;
  } else {
    control_reply_text(fd, 400, "Bad Request",
                       "name a live pad with &pad=0..%d, or &clear=1\n",
                       DINPUT_PAD_MAX - 1);
    return;
  }
  outcome = control_command_assignment((unsigned)(player_number - 1), target,
                                       reason, sizeof reason);
  if (!delivered(fd, outcome, reason,
                 "the guest did not poll within 5s, so the assignment was "
                 "NOT applied\n")) {
    return;
  }
  control_reply_text(fd, 200, "OK", "player %d: %s\n", player_number, reason);
}
