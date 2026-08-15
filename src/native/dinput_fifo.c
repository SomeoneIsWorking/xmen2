/*
 * LIVE key injection for a run being driven by hand.
 *
 * The scripted path (dinput_script.c) fixes its presses at launch, which is
 * the right thing for a reproducible headless measurement and the wrong thing
 * for "press Return when the dialog opens". Separate file because they are
 * separate jobs: one is a recording, the other is a keyboard.
 */
#include "dinput_fifo.h"

#include "dinput_system.h"
#include "gpu_device.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef X2_WITH_SDL
#include <SDL3/SDL.h>
#endif

/*
 * X2_INPUT_FIFO=<path> -- LIVE key injection, for driving a run by hand.
 *
 * The scripted path above cannot answer "press Return when the dialog opens":
 * its presses are fixed at launch, and a run parked in a conversation has no
 * way forward once they are spent (a windowless run takes no keyboard focus,
 * so a human at the machine cannot drive it either). With this set, each poll
 * drains the named pipe and presses whatever key names arrived, one per line:
 *
 *   mkfifo scratch/run/keys.fifo
 *   echo Return > scratch/run/keys.fifo
 *
 * A press is held FIFO_HOLD_S seconds so the game's per-frame update cannot
 * miss it, and is REPORTED exactly like a scripted press, so a driven run's
 * log still shows every input it acted on. Unknown names are refused by name,
 * never silently dropped -- an instrument that quietly does nothing is how a
 * parked dialog gets read as a game bug.
 */
#define FIFO_HOLD_S 0.30
#define FIFO_MAX_KEYS 8
static int g_fifo_fd = -2;          /* -2 = not tried yet */
static double g_fifo_next_try;
static struct {
    unsigned char dik;
    double until;
    int down;
    char name[24];
} g_fifo[FIFO_MAX_KEYS];
static char g_fifo_partial[64];
static int g_fifo_partial_len;

static void fifo_open_if_due(double now)
{
    const char *path;
    if (g_fifo_fd != -2) return;    /* open, or permanently disabled below */
    if (now < g_fifo_next_try) return;
    g_fifo_next_try = now + 1.0;
    path = getenv("X2_INPUT_FIFO");
    if (!path || !*path) { g_fifo_fd = -1; return; }
    /* O_RDWR so the open never blocks on a writer and never sees EOF. */
    g_fifo_fd = open(path, O_RDWR | O_NONBLOCK);
    if (g_fifo_fd < 0) {
        g_fifo_fd = -2;             /* try again in a second */
        return;
    }
    fprintf(stderr, "DINPUT8: X2_INPUT_FIFO=%s is OPEN -- write a key name "
                    "per line to press it.\n", path);
}

static void fifo_press(const char *name, double now)
{
#ifdef X2_WITH_SDL
    int scancode, i, slot = -1;
    unsigned char dik;
    scancode = (int)SDL_GetScancodeFromName(name);
    dik = scancode == SDL_SCANCODE_UNKNOWN
          ? 0 : dinput_system_dik(scancode);
    if (!dik) {
        fprintf(stderr, "DINPUT8: X2_INPUT_FIFO asked for \"%s\", which has "
                        "no DirectInput mapping. NOT pressed.\n", name);
        return;
    }
    for (i = 0; i < FIFO_MAX_KEYS; i++)
        if (!g_fifo[i].down && (slot < 0 || g_fifo[i].until < g_fifo[slot].until))
            slot = i;
    if (slot < 0) return;           /* all held; the next line still works */
    g_fifo[slot].dik = dik;
    g_fifo[slot].until = now + FIFO_HOLD_S;
    snprintf(g_fifo[slot].name, sizeof g_fifo[slot].name, "%s", name);
#else
    (void)name; (void)now;
#endif
}

static void fifo_drain(double now)
{
    char buf[256];
    ssize_t n;
    if (g_fifo_fd < 0) return;
    while ((n = read(g_fifo_fd, buf, sizeof buf)) > 0) {
        ssize_t i;
        for (i = 0; i < n; i++) {
            char c = buf[i];
            if (c != '\n' && c != '\r' && c != ',' && c != ' ' &&
                g_fifo_partial_len < (int)sizeof g_fifo_partial - 1) {
                g_fifo_partial[g_fifo_partial_len++] = c;
                continue;
            }
            if (g_fifo_partial_len) {
                g_fifo_partial[g_fifo_partial_len] = '\0';
                fifo_press(g_fifo_partial, now);
                g_fifo_partial_len = 0;
            }
        }
    }
}

void dinput_fifo_apply(uint32_t out, uint32_t size, double now)
{
    int i;
    if (g_fifo_fd == -2) fifo_open_if_due(now);
    fifo_drain(now);
    for (i = 0; i < FIFO_MAX_KEYS; i++) {
        int down = now < g_fifo[i].until;
        if (down && !g_fifo[i].down)
            fprintf(stderr, "DINPUT8: INJECTING \"%s\" (DIK 0x%02x) at "
                            "t=%.2fs, frame %lu  [fifo]\n", g_fifo[i].name,
                            g_fifo[i].dik, now, gpu_frames_presented());
        else if (!down && g_fifo[i].down)
            fprintf(stderr, "DINPUT8: released \"%s\" at t=%.2fs, frame %lu  "
                            "[fifo]\n", g_fifo[i].name, now,
                            gpu_frames_presented());
        if (down && (uint32_t)g_fifo[i].dik < size)
            *((unsigned char *)(uintptr_t)out + g_fifo[i].dik) = 0x80;
        g_fifo[i].down = down;
    }
}

