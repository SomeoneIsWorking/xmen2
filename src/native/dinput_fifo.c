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
#include "control.h"

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
    const char *via;            /* which channel pressed it, for the log */
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

/*
 * Press a key by name, from whichever channel asked -- the FIFO or the control
 * socket. One table, because the game has one keyboard: two channels with two
 * tables would each overwrite the other's byte in the same DirectInput buffer
 * and neither would be able to say so.
 *
 * Returns 0 and says why on a name with no DirectInput mapping, or when every
 * slot is currently held. Never silently does nothing: a caller that pressed a
 * key and saw no effect must be able to tell "the game ignored it" from "it was
 * never pressed", and that distinction is the whole value of a live channel.
 */
int dinput_inject_press(const char *name, double now, double hold,
                        const char *via, char *why, int whyn)
{
#ifdef X2_WITH_SDL
    int scancode, i, slot = -1;
    unsigned char dik;
    scancode = (int)SDL_GetScancodeFromName(name);
    dik = scancode == SDL_SCANCODE_UNKNOWN
          ? 0 : dinput_system_dik(scancode);
    if (!dik) {
        snprintf(why, (size_t)whyn,
                 "\"%s\" has no DirectInput mapping (SDL knows no such key "
                 "name, or it has no DIK code). NOT pressed.", name);
        fprintf(stderr, "DINPUT8: %s [%s]\n", why, via);
        return 0;
    }
    for (i = 0; i < FIFO_MAX_KEYS; i++)
        if (!g_fifo[i].down && (slot < 0 || g_fifo[i].until < g_fifo[slot].until))
            slot = i;
    if (slot < 0) {
        snprintf(why, (size_t)whyn,
                 "all %d injection slots are currently held down; \"%s\" was "
                 "NOT pressed. Wait for a hold to expire.", FIFO_MAX_KEYS, name);
        return 0;
    }
    g_fifo[slot].dik = dik;
    g_fifo[slot].until = now + (hold > 0.0 ? hold : FIFO_HOLD_S);
    g_fifo[slot].via = via;
    snprintf(g_fifo[slot].name, sizeof g_fifo[slot].name, "%s", name);
    return 1;
#else
    (void)name; (void)now; (void)hold; (void)via;
    snprintf(why, (size_t)whyn, "built without SDL, so there is no key name "
                                "table and nothing can be pressed.");
    return 0;
#endif
}

static void fifo_press(const char *name, double now)
{
    char why[160];
    if (!dinput_inject_press(name, now, 0.0, "fifo", why, (int)sizeof why))
        fprintf(stderr, "DINPUT8: X2_INPUT_FIFO: %s\n", why);
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
    /* Same poll, same table: the control socket's queued commands are applied
       here, on the thread that owns guest input, never on the server's. */
    control_pump(now);
    for (i = 0; i < FIFO_MAX_KEYS; i++) {
        int down = now < g_fifo[i].until;
        if (down && !g_fifo[i].down)
            fprintf(stderr, "DINPUT8: INJECTING \"%s\" (DIK 0x%02x) at "
                            "t=%.2fs, frame %lu  [%s]\n", g_fifo[i].name,
                            g_fifo[i].dik, now, gpu_frames_presented(),
                            g_fifo[i].via ? g_fifo[i].via : "fifo");
        else if (!down && g_fifo[i].down)
            fprintf(stderr, "DINPUT8: released \"%s\" at t=%.2fs, frame %lu  "
                            "[%s]\n", g_fifo[i].name, now,
                            gpu_frames_presented(),
                            g_fifo[i].via ? g_fifo[i].via : "fifo");
        if (down && (uint32_t)g_fifo[i].dik < size)
            *((unsigned char *)(uintptr_t)out + g_fifo[i].dik) = 0x80;
        g_fifo[i].down = down;
    }
}

