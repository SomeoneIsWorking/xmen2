#include "control.h"
#include "x86_reached.h"

#include "control_query.h"
#include "control_screenshot.h"
#include "control_status.h"
#include "autosave_runtime.h"
#include "dinput_fifo.h"
#include "dinput_pad.h"
#include "gpu_capture.h"
#include "gpu_device.h"
#include "input_probe.h"
#include "save_trace_runtime.h"
#include "transient_controller_assignment.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

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
    CMD_NONE = 0, CMD_KEY, CMD_SHOT, CMD_PAD, CMD_INPUT, CMD_SAVE,
    CMD_ASSIGNMENT
};

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_ready = PTHREAD_COND_INITIALIZER;
static pthread_cond_t  g_done = PTHREAD_COND_INITIALIZER;

static int    g_cmd;
static char   g_cmd_key[32];
static unsigned g_cmd_controller;
static double g_cmd_hold, g_cmd_value;
static int    g_cmd_ok;
static char   g_cmd_why[192];
static char  *g_probe;                 /* input snapshot, server-thread owned */
static size_t g_probe_len;
static X2ControlScreenshot g_screenshot;
static int g_shot_abandoned;

static int g_port;
static unsigned long g_requests, g_keys_pressed, g_keys_refused, g_shots;
static unsigned long g_pad_ok, g_pad_refused, g_probes, g_save_probes;

/* ---------------------------------------------------------------- pump --- */

#define PROBE_BYTES 16384u

void control_pump(CPU *cpu, double now)
{
    int cmd;

    x2_autosave_runtime_poll(cpu);
    if (!g_port) return;
    pthread_mutex_lock(&g_lock);
    cmd = g_cmd;
    if (cmd == CMD_NONE) { pthread_mutex_unlock(&g_lock); return; }

    /* Presentation-owned requests are serviced at the render boundary. */
    if (cmd == CMD_SHOT) { pthread_mutex_unlock(&g_lock); return; }

    if (cmd == CMD_KEY) {
        g_cmd_ok = dinput_inject_press(g_cmd_key, now, g_cmd_hold, "control",
                                       g_cmd_why, (int)sizeof g_cmd_why);
        if (g_cmd_ok) { g_keys_pressed++; g_cmd_why[0] = '\0'; }
        else g_keys_refused++;
    } else if (cmd == CMD_PAD) {
        g_cmd_ok = dinput_pad_virtual_set(g_cmd_key, g_cmd_value, g_cmd_hold,
                                          g_cmd_why, (int)sizeof g_cmd_why);
        if (g_cmd_ok) g_pad_ok++;      /* g_cmd_why carries the read-back */
        else g_pad_refused++;
    } else if (cmd == CMD_ASSIGNMENT) {
        if (g_cmd_value < 0.0) {
            x2_transient_controller_clear_player(g_cmd_controller);
            g_cmd_ok = 1;
            snprintf(g_cmd_why, sizeof g_cmd_why, "session assignment cleared");
        } else {
            g_cmd_ok = x2_transient_controller_assign((int)g_cmd_value,
                                                       g_cmd_controller);
            snprintf(g_cmd_why, sizeof g_cmd_why, "%s",
                     g_cmd_ok ? "session assignment applied" :
                     "that live pad cannot be assigned to that player");
        }
    } else if (cmd == CMD_INPUT) {
        if (!g_probe) g_probe = (char *)malloc(PROBE_BYTES);
        if (!g_probe) {
            g_cmd_ok = 0;
            snprintf(g_cmd_why, sizeof g_cmd_why,
                     "could not allocate the %u-byte report buffer",
                     PROBE_BYTES);
        } else {
            g_probe_len = input_probe_report(cpu, g_cmd_controller,
                                             g_probe, PROBE_BYTES);
            g_cmd_ok = g_probe_len != 0;
            if (g_cmd_ok) g_probes++;
            else snprintf(g_cmd_why, sizeof g_cmd_why,
                          "the input probe wrote nothing, which it is written "
                          "not to do -- treat this as a bug in the probe");
        }
    } else if (cmd == CMD_SAVE) {
        if (!g_probe) g_probe = (char *)malloc(PROBE_BYTES);
        if (!g_probe) {
            g_cmd_ok = 0;
            snprintf(g_cmd_why, sizeof g_cmd_why,
                     "could not allocate the %u-byte save report buffer",
                     PROBE_BYTES);
        } else {
            g_probe_len = x2_save_trace_runtime_report(g_probe, PROBE_BYTES);
            g_cmd_ok = g_probe_len != 0;
            if (g_cmd_ok) g_save_probes++;
            else snprintf(g_cmd_why, sizeof g_cmd_why,
                          "the save report exceeded its %u-byte bound; collect "
                          "fewer events or increase the production bound",
                          PROBE_BYTES);
        }
    }

    g_cmd = CMD_NONE;
    pthread_cond_signal(&g_done);
    pthread_mutex_unlock(&g_lock);
}

static void control_frame_pump(void)
{
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

    result = x2_control_screenshot_poll(
        &g_screenshot, g_cmd_why, (int)sizeof g_cmd_why);
    if (result == X2_CONTROL_SCREENSHOT_PENDING) {
        pthread_mutex_unlock(&g_lock);
        return;
    }
    g_cmd_ok = result == X2_CONTROL_SCREENSHOT_READY;
    if (g_cmd_ok) g_shots++;
    g_cmd = CMD_NONE;
    pthread_cond_signal(&g_done);
    pthread_mutex_unlock(&g_lock);
}

/* Hand work to its owning runtime thread and wait for it. A timeout is itself
   an answer: the relevant guest-input or presentation boundary did not run. */
static int submit(int cmd, double timeout_s)
{
    struct timespec ts;
    int rc = 0;

    pthread_mutex_lock(&g_lock);
    g_cmd = cmd;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += (time_t)timeout_s;
    while (g_cmd != CMD_NONE)
        if (pthread_cond_timedwait(&g_done, &g_lock, &ts) != 0) break;
    rc = (g_cmd == CMD_NONE);
    if (!rc && cmd == CMD_SHOT) g_shot_abandoned = 1;
    g_cmd = CMD_NONE;
    pthread_mutex_unlock(&g_lock);
    return rc;
}

/* -------------------------------------------------------------- serving --- */

static void send_all(int fd, const void *p, size_t n)
{
    const char *b = (const char *)p;
    while (n) {
        ssize_t k = write(fd, b, n);
        if (k <= 0) return;
        b += k; n -= (size_t)k;
    }
}

static void reply(int fd, int code, const char *status, const char *ctype,
                  const void *body, size_t n)
{
    char head[256];
    int hn = snprintf(head, sizeof head,
                      "HTTP/1.1 %d %s\r\nContent-Type: %s\r\n"
                      "Content-Length: %zu\r\nConnection: close\r\n\r\n",
                      code, status, ctype, n);
    send_all(fd, head, (size_t)hn);
    if (n) send_all(fd, body, n);
}

void control_reply_text(int fd, int code, const char *status, const char *fmt, ...)
{
    char body[1024];
    int n;
    va_list ap;
    va_start(ap, fmt);
    n = vsnprintf(body, sizeof body, fmt, ap);
    va_end(ap);
    reply(fd, code, status, "text/plain; charset=utf-8", body, (size_t)n);
}

static void route_status(int fd)
{
    char body[4096];
    size_t size = control_status_format(body, sizeof body, g_requests,
                                        g_keys_pressed, g_keys_refused, g_shots);
    if (!size) {
        control_reply_text(fd, 500, "Internal Server Error",
                   "live status exceeded its bounded response buffer\n");
        return;
    }
    reply(fd, 200, "OK", "application/json", body, size);
}

static void route_key(int fd, const char *query)
{
    char name[32] = "", hold[16] = "";

    if (!control_query_arg(query, "name", name, sizeof name) || !name[0]) {
        control_reply_text(fd, 400, "Bad Request",
                   "no key named. Use /key?name=Return[&hold=0.3].\n"
                   "Names are SDL scancode names: Return, Escape, Up, Down,\n"
                   "Left, Right, Space, A, 1, F1 ...\n");
        return;
    }
    g_cmd_hold = control_query_arg(query, "hold", hold, sizeof hold)
                 ? atof(hold) : 0.0;
    snprintf(g_cmd_key, sizeof g_cmd_key, "%s", name);

    if (!submit(CMD_KEY, 5.0)) {
        control_reply_text(fd, 504, "Gateway Timeout",
                   "the guest did not poll its keyboard within 5s, so \"%s\" "
                   "was NOT pressed.\nThat is a statement about the RUN, not "
                   "about this channel: the game is stuck, still loading, or "
                   "has not reached its input loop.\n", name);
        return;
    }
    if (!g_cmd_ok) { control_reply_text(fd, 409, "Conflict", "%s\n", g_cmd_why); return; }
    control_reply_text(fd, 200, "OK", "pressed \"%s\" for %.2fs at frame %lu\n",
               name, g_cmd_hold > 0.0 ? g_cmd_hold : 0.30,
               gpu_frames_presented());
}

static void route_pad(int fd, const char *query)
{
    char what[32] = "", hold[16] = "", value[16] = "";

    if (!control_query_arg(query, "button", what, sizeof what) &&
        !control_query_arg(query, "axis", what, sizeof what)) {
        control_reply_text(fd, 400, "Bad Request",
                   "no button or axis named.\n"
                   "  /pad?button=a[&hold=0.3]\n"
                   "  /pad?axis=leftx&value=-1[&hold=0.5]\n"
                   "Buttons: a b x y back start leftstick rightstick "
                   "leftshoulder rightshoulder\n"
                   "Axes: leftx lefty rightx righty lefttrigger righttrigger, "
                   "value -1..1\n");
        return;
    }
    g_cmd_hold  = control_query_arg(query, "hold", hold, sizeof hold)
                  ? atof(hold) : 0.0;
    g_cmd_value = control_query_arg(query, "value", value, sizeof value)
                  ? atof(value) : 1.0;
    snprintf(g_cmd_key, sizeof g_cmd_key, "%s", what);

    if (!submit(CMD_PAD, 5.0)) {
        control_reply_text(fd, 504, "Gateway Timeout",
                   "the guest did not poll within 5s, so \"%s\" was NOT set.\n",
                   what);
        return;
    }
    if (!g_cmd_ok) { control_reply_text(fd, 409, "Conflict", "%s\n", g_cmd_why); return; }
    control_reply_text(fd, 200, "OK", "pad \"%s\" set at frame %lu -- %s\n",
               what, gpu_frames_presented(), g_cmd_why);
}

static void route_assignment(int fd, const char *query)
{
    char player[8] = "", pad[8] = "", clear[8] = "";
    int player_number, pad_number;
    if (!control_query_arg(query, "player", player, sizeof player) ||
        !bounded_number(player, 1, 4, &player_number)) {
        control_reply_text(fd, 400, "Bad Request",
                   "use /assignment?player=1..4&pad=N or &clear=1\n");
        return;
    }
    g_cmd_controller = (unsigned)(player_number - 1);
    if (control_query_arg(query, "clear", clear, sizeof clear) && atoi(clear))
        g_cmd_value = -1.0;
    else if (control_query_arg(query, "pad", pad, sizeof pad) &&
             bounded_number(pad, 0, DINPUT_PAD_MAX - 1, &pad_number))
        g_cmd_value = (double)pad_number;
    else {
        control_reply_text(fd, 400, "Bad Request", "no live pad or clear requested\n");
        return;
    }
    if (!submit(CMD_ASSIGNMENT, 5.0)) {
        control_reply_text(fd, 504, "Gateway Timeout",
                   "the guest did not poll within 5s; assignment unchanged\n");
        return;
    }
    if (!g_cmd_ok) { control_reply_text(fd, 409, "Conflict", "%s\n", g_cmd_why); return; }
    control_reply_text(fd, 200, "OK", "player %d: %s\n", player_number, g_cmd_why);
}

static void route_shot(int fd)
{
    const unsigned char *png;
    size_t png_bytes;

    if (!submit(CMD_SHOT, 10.0)) {
        control_reply_text(fd, 504, "Gateway Timeout",
                   "the renderer did not complete a capturable frame within "
                   "10s.\nThe run is stuck, still loading, or not presenting "
                   "-- ask /status for its frame count.\n");
        return;
    }
    if (!g_cmd_ok) { control_reply_text(fd, 409, "Conflict", "%s\n", g_cmd_why); return; }
    png = x2_control_screenshot_png(&g_screenshot, &png_bytes);
    reply(fd, 200, "OK", "image/png", png, png_bytes);
}

static void route_input(int fd, const char *query)
{
    char which[16] = "";
    g_cmd_controller = control_query_arg(query, "controller", which,
                                         sizeof which)
                       ? (unsigned)atoi(which) : 0u;
    if (!submit(CMD_INPUT, 10.0)) {
        control_reply_text(fd, 504, "Gateway Timeout",
                   "the guest did not reach an input poll within 10s, so its "
                   "binding table was not read.\nThat is a statement about the "
                   "RUN: it is stuck, still loading, or has not reached its "
                   "input loop.\n");
        return;
    }
    if (!g_cmd_ok) { control_reply_text(fd, 409, "Conflict", "%s\n", g_cmd_why); return; }
    reply(fd, 200, "OK", "text/plain; charset=utf-8", g_probe, g_probe_len);
}

static void route_save(int fd)
{
    if (!submit(CMD_SAVE, 10.0)) {
        control_reply_text(fd, 504, "Gateway Timeout",
                   "the guest did not reach an input poll within 10s, so the "
                   "save trace was not read. The run is stuck, still loading, "
                   "or has not reached its input loop.\n");
        return;
    }
    if (!g_cmd_ok) { control_reply_text(fd, 409, "Conflict", "%s\n", g_cmd_why); return; }
    reply(fd, 200, "OK", "text/plain; charset=utf-8", g_probe, g_probe_len);
}

static void serve(int fd)
{
    char req[1024], *path, *query, *sp;
    ssize_t n = read(fd, req, sizeof req - 1);

    if (n <= 0) return;
    req[n] = '\0';
    g_requests++;

    path = strchr(req, ' ');
    if (!path) { control_reply_text(fd, 400, "Bad Request", "unparseable request\n"); return; }
    path++;
    sp = strchr(path, ' ');
    if (sp) *sp = '\0';
    query = strchr(path, '?');
    if (query) *query++ = '\0';

    if (!strcmp(path, "/status"))          route_status(fd);
    else if (!strcmp(path, "/key"))        route_key(fd, query ? query : "");
    else if (!strcmp(path, "/pad"))        route_pad(fd, query ? query : "");
    else if (!strcmp(path, "/assignment")) route_assignment(fd, query ? query : "");
    else if (!strcmp(path, "/screenshot")) route_shot(fd);
    else if (!strcmp(path, "/input"))      route_input(fd, query ? query : "");
    else if (!strcmp(path, "/save"))       route_save(fd);
    else if (!strcmp(path, "/reached"))    x86_reached_route(fd, query ? query : "");
    else
        control_reply_text(fd, 404, "Not Found",
                   "no such endpoint: %s\n"
                   "  GET /status       frames, guest time, frame timing\n"
                   "  GET /key?name=X   press a key (&hold=<seconds>)\n"
                   "  GET /pad?button=a press a SYNTHETIC pad button (&hold=)\n"
                   "  GET /pad?axis=leftx&value=-1   move an axis\n"
                   "  GET /assignment?player=P&pad=N session-only ownership\n"
                   "  GET /screenshot   the current frame, as a PNG\n"
                   "  GET /input[?controller=N]  the GAME's binding table "
                   "and which actions read down\n"
                   "  GET /save         bounded retail save/load trace\n"
                   "  GET /reached      has the game ever entered that "
                   "function? (self-documents)\n",
                   path);
}

static void *server_thread(void *arg)
{
    int lfd = (int)(intptr_t)arg;
    for (;;) {
        int fd = accept(lfd, NULL, NULL);
        if (fd < 0) { if (errno == EINTR) continue; break; }
        serve(fd);
        close(fd);
    }
    return NULL;
}

/* ---------------------------------------------------------------- start --- */

int control_start(int port)
{
    struct sockaddr_in a;
    pthread_t th;
    int lfd, on = 1;

    if (!port) {
        const char *e = getenv("X2_CONTROL");
        port = (e && *e) ? atoi(e) : 0;
    }
    if (!port) return 0;

    lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd < 0) {
        fprintf(stderr, "control: socket() failed: %s. REFUSING to run without "
                        "the control channel that was asked for.\n",
                strerror(errno));
        exit(2);
    }
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);     /* loopback ONLY */
    a.sin_port = htons((unsigned short)port);
    if (bind(lfd, (struct sockaddr *)&a, sizeof a) < 0 || listen(lfd, 8) < 0) {
        fprintf(stderr, "control: cannot listen on 127.0.0.1:%d: %s.\n"
                        "REFUSING rather than running deaf -- a run that "
                        "silently failed to bind ignores every command while "
                        "looking healthy.\n", port, strerror(errno));
        exit(2);
    }
    g_port = port;
    gpu_capture_set_frame_observer(control_frame_pump);
    printf("control: http://127.0.0.1:%d  -- /status /key?name=X /pad "
           "/screenshot /input /save\n"
           "control: loopback only; guest commands use its input poll and "
           "screenshots use the render boundary, never the server thread.\n",
           port);
    fflush(stdout);
    pthread_create(&th, NULL, server_thread, (void *)(intptr_t)lfd);
    pthread_detach(th);
    return port;
}

void control_report(void)
{
    if (!g_port) {
        fprintf(stderr, "  control: not started (no --control / X2_CONTROL), "
                        "so this run took no live commands.\n");
        return;
    }
    fprintf(stderr,
        "  control: port %d served %lu request(s) -- %lu key(s) pressed, %lu "
        "refused by name or slot, %lu screenshot(s),\n"
        "           %lu synthetic pad input(s) set and %lu refused, %lu "
        "input probe(s), %lu save probe(s).\n",
        g_port, g_requests, g_keys_pressed, g_keys_refused, g_shots,
        g_pad_ok, g_pad_refused, g_probes, g_save_probes);
}
