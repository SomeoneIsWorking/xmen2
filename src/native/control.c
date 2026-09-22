#include "control.h"
#include "../config/environment.h"
#include "control_command_bridge.h"
#include "control_http.h"
#include "control_input_route.h"
#include "x2_log.h"

#include "../input/touch_inject.h"
#include "autosave_runtime.h"
#include "control_performance_route.h"
#include "control_query.h"
#include "control_save_route.h"
#include "control_screenshot.h"
#include "control_status.h"
#include "control_status_route.h"
#include "control_ui_route.h"
#include "dinput_fifo.h"
#include "dinput_pad.h"
#include "gpu_capture.h"
#include "gpu_device.h"
#include "gpu_frame_timing.h"
#include "input_probe.h"
#include "save_trace_runtime.h"
#include "transient_controller_assignment.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include "platform_threads.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*
 * ONE command in flight at a time, handed across a mutex.
 *
 * The server thread must never touch guest state: the guest is single-threaded
 * under a cooperative scheduler, and reading its input table from outside
 * that schedule is exactly the kind of race that produces
 * an intermittent bug nobody can reproduce. So the server parks a request here
 * and waits; control_pump, running on the thread that owns guest input,
 * performs guest-state work and wakes the server with the answer. Screenshots
 * are presentation work and are instead drained by control_frame_pump on the
 * render thread after the final composed frame has completed its readback.
 */
enum {
  CMD_NONE = 0,
  CMD_KEY,
  CMD_SHOT,
  CMD_PAD,
  CMD_INPUT,
  CMD_SAVE,
  CMD_ASSIGNMENT,
  CMD_TOUCH,
  CMD_PERFORMANCE_RESET
};

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_ready = PTHREAD_COND_INITIALIZER;
static pthread_cond_t g_done = PTHREAD_COND_INITIALIZER;

static int g_cmd;
static char g_cmd_key[32];
static unsigned g_cmd_controller;
static double g_cmd_hold, g_cmd_value;
static double g_cmd_x, g_cmd_y; /* normalized contact position */
static int g_cmd_phase;
static int g_cmd_ok;
static char g_cmd_why[192];
static char *g_probe; /* input snapshot, server-thread owned */
static size_t g_probe_len;
static X2ControlScreenshot g_screenshot;
static int g_shot_abandoned;

static int g_port;
static unsigned long g_requests, g_keys_pressed, g_keys_refused, g_shots;
static unsigned long g_pad_ok, g_pad_refused, g_probes, g_save_probes;

/* ---------------------------------------------------------------- pump --- */

#define PROBE_BYTES 16384u

void control_pump(CPU *cpu, double now) {
  int cmd;

  x2_autosave_runtime_poll(cpu);
  if (!g_port)
    return;
  pthread_mutex_lock(&g_lock);
  cmd = g_cmd;
  if (cmd == CMD_NONE) {
    pthread_mutex_unlock(&g_lock);
    return;
  }

  /* Presentation-owned requests are serviced at the render boundary. */
  if (cmd == CMD_SHOT) {
    pthread_mutex_unlock(&g_lock);
    return;
  }

  if (cmd == CMD_KEY) {
    g_cmd_ok = dinput_inject_press(g_cmd_key, now, g_cmd_hold, "control",
                                   g_cmd_why, (int)sizeof g_cmd_why);
    if (g_cmd_ok) {
      g_keys_pressed++;
      g_cmd_why[0] = '\0';
    } else
      g_keys_refused++;
  } else if (cmd == CMD_PAD) {
    g_cmd_ok = dinput_pad_virtual_set(g_cmd_key, g_cmd_value, g_cmd_hold,
                                      g_cmd_why, (int)sizeof g_cmd_why);
    if (g_cmd_ok)
      g_pad_ok++; /* g_cmd_why carries the read-back */
    else
      g_pad_refused++;
  } else if (cmd == CMD_ASSIGNMENT) {
    if (g_cmd_value < 0.0) {
      x2_transient_controller_clear_player(g_cmd_controller);
      g_cmd_ok = 1;
      snprintf(g_cmd_why, sizeof g_cmd_why, "session assignment cleared");
    } else {
      g_cmd_ok =
          x2_transient_controller_assign((int)g_cmd_value, g_cmd_controller);
      snprintf(g_cmd_why, sizeof g_cmd_why, "%s",
               g_cmd_ok ? "session assignment applied"
                        : "that live pad cannot be assigned to that player");
    }
  } else if (cmd == CMD_TOUCH) {
    /* Goes through the runtime's own injector, which takes the same
       note-source and routing calls the host event pump takes. A separate
       copy here could only agree with the shipping path by luck. */
    g_cmd_ok = x2_touch_inject(1, (float)g_cmd_x, (float)g_cmd_y,
                               (X2TouchPhase)g_cmd_phase);
    /* What the contact then DID is the touch census's account, not a second
       tally here that could disagree with it. */
    snprintf(g_cmd_why, sizeof g_cmd_why,
             g_cmd_ok ? "the contact was routed"
                      : "NOTHING routed it: there is no window, or the "
                        "contact was not a finger event");
  } else if (cmd == CMD_INPUT) {
    if (!g_probe)
      g_probe = (char *)malloc(PROBE_BYTES);
    if (!g_probe) {
      g_cmd_ok = 0;
      snprintf(g_cmd_why, sizeof g_cmd_why,
               "could not allocate the %u-byte report buffer", PROBE_BYTES);
    } else {
      g_probe_len =
          input_probe_report(cpu, g_cmd_controller, g_probe, PROBE_BYTES);
      g_cmd_ok = g_probe_len != 0;
      if (g_cmd_ok)
        g_probes++;
      else
        snprintf(g_cmd_why, sizeof g_cmd_why,
                 "the input probe wrote nothing, which it is written "
                 "not to do -- treat this as a bug in the probe");
    }
  } else if (cmd == CMD_SAVE) {
    if (!g_probe)
      g_probe = (char *)malloc(PROBE_BYTES);
    if (!g_probe) {
      g_cmd_ok = 0;
      snprintf(g_cmd_why, sizeof g_cmd_why,
               "could not allocate the %u-byte save report buffer",
               PROBE_BYTES);
    } else {
      g_probe_len = x2_save_trace_runtime_report(g_probe, PROBE_BYTES);
      g_cmd_ok = g_probe_len != 0;
      if (g_cmd_ok)
        g_save_probes++;
      else
        snprintf(g_cmd_why, sizeof g_cmd_why,
                 "the save report exceeded its %u-byte bound; collect "
                 "fewer events or increase the production bound",
                 PROBE_BYTES);
    }
  } else if (cmd == CMD_PERFORMANCE_RESET) {
    gpu_frame_timing_reset();
    g_cmd_ok = 1;
    snprintf(
        g_cmd_why, sizeof g_cmd_why,
        "frame-time qualification window reset at the guest input boundary");
  }

  g_cmd = CMD_NONE;
  pthread_cond_signal(&g_done);
  pthread_mutex_unlock(&g_lock);
}

static void control_frame_pump(void) {
  int result;

  pthread_mutex_lock(&g_lock);
  if (g_shot_abandoned) {
    x2_control_screenshot_abandon(&g_screenshot);
    g_shot_abandoned = 0;
  }
  if (g_cmd != CMD_SHOT) {
    pthread_mutex_unlock(&g_lock);
    return;
  }

  result = x2_control_screenshot_poll(&g_screenshot, g_cmd_why,
                                      (int)sizeof g_cmd_why);
  if (result == X2_CONTROL_SCREENSHOT_PENDING) {
    pthread_mutex_unlock(&g_lock);
    return;
  }
  g_cmd_ok = result == X2_CONTROL_SCREENSHOT_READY;
  if (g_cmd_ok)
    g_shots++;
  g_cmd = CMD_NONE;
  pthread_cond_signal(&g_done);
  pthread_mutex_unlock(&g_lock);
}

/* Hand work to its owning runtime thread and wait for it. A timeout is itself
   an answer: the relevant guest-input or presentation boundary did not run. */
static int submit(int cmd, double timeout_s) {
  struct timespec ts;
  int rc = 0;

  pthread_mutex_lock(&g_lock);
  g_cmd = cmd;
  clock_gettime(CLOCK_REALTIME, &ts);
  ts.tv_sec += (time_t)timeout_s;
  while (g_cmd != CMD_NONE)
    if (pthread_cond_timedwait(&g_done, &g_lock, &ts) != 0)
      break;
  rc = (g_cmd == CMD_NONE);
  if (!rc && cmd == CMD_SHOT)
    g_shot_abandoned = 1;
  g_cmd = CMD_NONE;
  pthread_mutex_unlock(&g_lock);
  return rc;
}

int control_command_save(const char **report, size_t *report_size, char *reason,
                         size_t reason_capacity) {
  if (!submit(CMD_SAVE, 10.0))
    return -1;
  snprintf(reason, reason_capacity, "%s", g_cmd_why);
  if (!g_cmd_ok)
    return 0;
  *report = g_probe;
  *report_size = g_probe_len;
  return 1;
}

int control_command_performance_reset(char *reason, size_t reason_capacity) {
  if (!submit(CMD_PERFORMANCE_RESET, 10.0))
    return -1;
  snprintf(reason, reason_capacity, "%s", g_cmd_why);
  return g_cmd_ok;
}

/* The counts belong here, with the queue that performs the work, and not with
   the route that parsed the request: /status reports them. */
int control_command_key(const char *name, double hold, char *reason,
                        size_t reason_capacity) {
  g_cmd_hold = hold;
  snprintf(g_cmd_key, sizeof g_cmd_key, "%s", name);
  if (!submit(CMD_KEY, 5.0))
    return -1;
  snprintf(reason, reason_capacity, "%s", g_cmd_why);
  return g_cmd_ok;
}

int control_command_pad(const char *what, double value, double hold,
                        char *reason, size_t reason_capacity) {
  g_cmd_hold = hold;
  g_cmd_value = value;
  snprintf(g_cmd_key, sizeof g_cmd_key, "%s", what);
  if (!submit(CMD_PAD, 5.0))
    return -1;
  snprintf(reason, reason_capacity, "%s", g_cmd_why);
  return g_cmd_ok;
}

int control_command_touch(double x, double y, int phase, char *reason,
                          size_t reason_capacity) {
  g_cmd_x = x;
  g_cmd_y = y;
  g_cmd_phase = phase;
  if (!submit(CMD_TOUCH, 5.0))
    return -1;
  snprintf(reason, reason_capacity, "%s", g_cmd_why);
  return g_cmd_ok;
}

int control_command_assignment(unsigned player_index, double pad_or_clear,
                               char *reason, size_t reason_capacity) {
  g_cmd_controller = player_index;
  g_cmd_value = pad_or_clear;
  if (!submit(CMD_ASSIGNMENT, 5.0))
    return -1;
  snprintf(reason, reason_capacity, "%s", g_cmd_why);
  return g_cmd_ok;
}

/* -------------------------------------------------------------- serving --- */

static void route_shot(x2_socket_t fd) {
  const unsigned char *png;
  size_t png_bytes;

  if (!submit(CMD_SHOT, 10.0)) {
    control_reply_text(
        fd, 504, "Gateway Timeout",
        "the renderer did not complete a capturable frame within "
        "10s.\nThe run is stuck, still loading, or not presenting "
        "-- ask /status for its frame count.\n");
    return;
  }
  if (!g_cmd_ok) {
    control_reply_text(fd, 409, "Conflict", "%s\n", g_cmd_why);
    return;
  }
  png = x2_control_screenshot_png(&g_screenshot, &png_bytes);
  control_reply_bytes(fd, 200, "OK", "image/png", png, png_bytes);
}

static void route_input(x2_socket_t fd, const char *query) {
  char which[16] = "";
  g_cmd_controller = control_query_arg(query, "controller", which, sizeof which)
                         ? (unsigned)atoi(which)
                         : 0u;
  if (!submit(CMD_INPUT, 10.0)) {
    control_reply_text(
        fd, 504, "Gateway Timeout",
        "the guest did not reach an input poll within 10s, so its "
        "binding table was not read.\nThat is a statement about the "
        "RUN: it is stuck, still loading, or has not reached its "
        "input loop.\n");
    return;
  }
  if (!g_cmd_ok) {
    control_reply_text(fd, 409, "Conflict", "%s\n", g_cmd_why);
    return;
  }
  control_reply_bytes(fd, 200, "OK", "text/plain; charset=utf-8", g_probe,
                      g_probe_len);
}

static void serve(x2_socket_t fd) {
  char req[1024], *path, *query, *sp;
  x2_socket_ssize_t n = x2_socket_recv(fd, req, sizeof req - 1);

  if (n <= 0)
    return;
  req[n] = '\0';
  g_requests++;

  path = strchr(req, ' ');
  if (!path) {
    control_reply_text(fd, 400, "Bad Request", "unparseable request\n");
    return;
  }
  path++;
  sp = strchr(path, ' ');
  if (sp)
    *sp = '\0';
  query = strchr(path, '?');
  if (query)
    *query++ = '\0';

  if (!strcmp(path, "/status"))
    control_status_route(fd, g_requests, g_keys_pressed, g_keys_refused,
                         g_shots);
  else if (!strcmp(path, "/key"))
    control_route_key(fd, query ? query : "");
  else if (!strcmp(path, "/ui/key"))
    control_ui_key_route(fd, query ? query : "");
  else if (!strcmp(path, "/ui/click"))
    control_ui_click_route(fd, query ? query : "");
  else if (!strcmp(path, "/pad"))
    control_route_pad(fd, query ? query : "");
  else if (!strcmp(path, "/touch"))
    control_route_touch(fd, query ? query : "");
  else if (!strcmp(path, "/prompts"))
    control_route_prompts(fd);
  else if (!strcmp(path, "/controls"))
    control_route_controls(fd);
  else if (!strcmp(path, "/assignment"))
    control_route_assignment(fd, query ? query : "");
  else if (!strcmp(path, "/screenshot"))
    route_shot(fd);
  else if (!strcmp(path, "/input"))
    route_input(fd, query ? query : "");
  else if (!strcmp(path, "/save"))
    control_save_route(fd);
  else if (!strcmp(path, "/performance/reset"))
    control_performance_reset_route(fd);
  else if (!strcmp(path, "/performance/probe"))
    control_performance_probe_route(fd, query ? query : "");
  else
    control_reply_text(
        fd, 404, "Not Found",
        "no such endpoint: %s\n"
        "  GET /status       frames, guest time, frame timing\n"
        "  GET /key?name=X   press a key (&hold=<seconds>)\n"
        "  GET /ui/key?name=F2  press a key at the PORT's own UI\n"
        "  GET /ui/click?x=X&y=Y  click at the PORT's own UI\n"
        "  GET /prompts      the action prompts a finger can press now\n"
        "  GET /controls     where the overlay's controls are, and the "
        "stick's deflection\n"
        "  GET /pad?button=a press a SYNTHETIC pad button (&hold=)\n"
        "  GET /pad?axis=leftx&value=-1   move an axis\n"
        "  GET /touch?x=0.5&y=0.9  press the screen where a finger would "
        "(&phase=down|motion|up|cancel)\n"
        "  GET /assignment?player=P&pad=N session-only ownership\n"
        "  GET /screenshot   the current frame, as a PNG\n"
        "  GET /input[?controller=N]  the GAME's binding table "
        "and which actions read down\n"
        "  GET /save         bounded retail save/load trace\n"
        "  GET /performance/reset  start a fresh frame-time window\n"
        "  GET /performance/probe?n=4096  arm the hot-guest-entry-point "
        "probe (n=0 disarms)\n"
        "  GET /reached      has the game ever entered that "
        "function? (self-documents)\n",
        path);
}

/* ---------------------------------------------------------------- start --- */

int control_start(int port) {

  if (!port) {
    const char *e = x2_config_override_get(kX2ConfigControl);
    port = (e && *e) ? atoi(e) : 0;
  }
  if (!port)
    return 0;

  if (!control_http_listen(port, serve))
    exit(2);
  g_port = port;
  gpu_capture_set_frame_observer(control_frame_pump);
  x2_log_info("control: http://127.0.0.1:%d  -- /status /key?name=X /pad "
              "/screenshot /input /save /performance/reset\n"
              "control: loopback only; guest commands use its input poll and "
              "screenshots use the render boundary, never the server thread.\n",
              port);
  return port;
}

void control_report(void) {
  if (!g_port) {
    x2_log_error("  control: not started (no --control / X2_CONTROL), "
                 "so this run took no live commands.\n");
    return;
  }
  x2_log_error(
      "  control: port %d served %lu request(s) -- %lu key(s) pressed, %lu "
      "refused by name or slot, %lu screenshot(s),\n"
      "           %lu synthetic pad input(s) set and %lu refused, %lu "
      "input probe(s), %lu save probe(s).\n",
      g_port, g_requests, g_keys_pressed, g_keys_refused, g_shots, g_pad_ok,
      g_pad_refused, g_probes, g_save_probes);
}
