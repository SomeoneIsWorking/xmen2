/* See heartbeat.h. */
#include "heartbeat.h"

#include "x86rt.h"
#include "x86rt_native.h"
#include "d3d8_device.h"
#include "gpu_draw.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static double   g_period = 5.0;
static double   g_t0;

/*
 * Set by the SIGTERM/SIGINT handler, read here.
 *
 * The run used to always END -- on a fault, an abort or an unimplemented
 * import -- so every report in this project is an atexit handler. Now that it
 * reaches a frame loop and keeps going, the ONLY way it stops is a kill, and a
 * signal handler cannot run those reports: they are stdio, and stdio in a
 * handler deadlocks against whatever the interrupted code was holding (issue
 * #34). This thread is ordinary context, so it can. The handler hands the job
 * over and arms an alarm in case this thread never gets there.
 */
volatile sig_atomic_t x2_report_now;

/* Whether there is a thread to hand that job to. */
static int g_running;
int heartbeat_running(void) { return g_running; }

/*
 * Its own thread, deliberately.
 *
 * Driving this from Present would tie the one instrument that answers "is the
 * loop alive" to the loop reaching Present -- so a run stuck BEFORE the
 * present would go silent, which is the case it exists for. A thread of its
 * own reports the same line whether the guest is running, spinning or blocked.
 *
 * It only READS counters (unsigned long loads that the guest thread only ever
 * increments), so there is no lock: a torn read would mis-state one delta on
 * one line, and taking a lock the guest thread holds would let a diagnostic
 * stall the run it is measuring. That trade is stated rather than assumed.
 */
static void *heartbeat_thread(void *arg)
{
    unsigned long p_cross = 0, p_scenes = 0, p_presents = 0, p_clears = 0,
                  p_draws = 0, p_gpu = 0, p_ref = 0;
    int first = 1, stalled = 0, dumped = 0;
    (void)arg;
    for (;;) {
        struct timespec req;
        unsigned long cross, scenes = 0, presents = 0, clears = 0, draws = 0;
        unsigned long gpu_draws = 0, gpu_refused = 0;
        int have_dev;
        double t;

        /* Slept in slices, not in one go: an interrupt has to be noticed in
           a quarter of a second, not at the end of a five-second period. */
        double slept = 0.0;
        while (slept < g_period && !x2_report_now) {
            req.tv_sec = 0;
            req.tv_nsec = 250000000L;
            while (nanosleep(&req, &req) != 0 && errno == EINTR) ;
            slept += 0.25;
        }
        if (x2_report_now) {
            fprintf(stderr, "\n[HB] interrupted -- the shutdown reports "
                            "follow, taken while the guest is STILL RUNNING, "
                            "so every count is a snapshot rather than a "
                            "final total.\n");
            fflush(stderr);
            x2_interrupt_reports();
            _exit(4);
        }

        t = now_s() - g_t0;
        cross = x86_crossings();
        have_dev = d3d8_device_counts(&scenes, &presents, &clears, &draws);
        gpu_draw_counts(&gpu_draws, &gpu_refused);

        if (first) {
            first = 0;
            p_cross = cross; p_scenes = scenes; p_presents = presents;
            p_clears = clears; p_draws = draws;
            p_gpu = gpu_draws; p_ref = gpu_refused;
            fprintf(stderr, "[HB] %6.1fs  the first line is a baseline; the "
                            "deltas below are per %.1fs.\n", t, g_period);
            fprintf(stderr, "[HB] %6.1fs  crossings %lu (%s)%s\n",
                    t, cross, x86_crossings_what(),
                    have_dev ? "" : "  -- no D3D8 device exists yet");
            continue;
        }

        if (cross == p_cross) {
            /* The one case that IS a hang, and it has to be said in words: a
               guest that executed nothing is not slow, it is stopped. */
            fprintf(stderr, "[HB] %6.1fs  the guest executed NOTHING in the "
                            "last %.1fs (crossings unchanged at %lu) -- it is "
                            "blocked inside host code or stopped, not "
                            "looping.\n", t, g_period, cross);
            continue;
        }

        fprintf(stderr, "[HB] %6.1fs  crossings %lu (+%lu)", t, cross,
                cross - p_cross);
        if (!have_dev) {
            fprintf(stderr, "  -- no D3D8 device exists, so there are no frame "
                            "counters to show yet.\n");
        } else {
            fprintf(stderr, "  scenes %lu (+%lu)  clears %lu (+%lu)  "
                            "draws %lu (+%lu)  presents %lu (+%lu)\n",
                    scenes, scenes - p_scenes, clears, clears - p_clears,
                    draws, draws - p_draws, presents, presents - p_presents);
            /* Each zero delta gets its own sentence. A row of numbers with a
               0 in it reads as noise; "no frame was presented" reads as the
               finding it is. */
            if (presents == p_presents)
                fprintf(stderr, "[HB]           ... and NO frame was presented "
                                "in that time (still %lu) -- the guest is "
                                "running, but not reaching Present.\n",
                        presents);
            if (draws == p_draws)
                fprintf(stderr, "[HB]           ... and NOTHING was drawn in "
                                "that time (still %lu) -- whatever frames ran "
                                "submitted no geometry.\n", draws);
            /* The engine's draw count and the GPU's are different claims: the
               first is what was asked for, the second what was rasterised. A
               black screen with both rising is a shading problem; a black
               screen with the second flat is a backend that refused every
               draw, and only these two numbers side by side say which. */
            fprintf(stderr, "[HB]           gpu draws %lu (+%lu)  refused %lu "
                            "(+%lu)%s\n",
                    gpu_draws, gpu_draws - p_gpu, gpu_refused,
                    gpu_refused - p_ref,
                    gpu_draws == p_gpu && draws != p_draws
                    ? "  -- the engine asked and the BACKEND drew none" : "");
        }
        /*
         * A stall -- executing, not presenting -- dumps the ring, ONCE.
         *
         * This is the state issue #35 was: 1051 frames at 60fps and then the
         * frame function is never entered again while the guest keeps running
         * millions of crossings a second. Everything that could say what it is
         * doing (the ring) used to be reachable only by killing the run, and
         * the kill path is a signal handler where stdio deadlocks. From this
         * thread it is an ordinary call.
         *
         * The guest is still writing the ring while it is read, so an entry
         * can be torn. That is stated in the header rather than prevented: a
         * lock here would let a diagnostic stall the run it is measuring.
         */
        if (have_dev && presents == p_presents && cross != p_cross) {
            if (++stalled == 2 && !dumped) {
                dumped = 1;
                fprintf(stderr, "[HB] the guest is EXECUTING but has presented "
                                "nothing for %.1fs. Dumping the boundary ring "
                                "as a snapshot -- the guest is still running, "
                                "so a line may be torn. Reported once.\n",
                        2 * g_period);
                x86_ring_dump();
            }
        } else {
            stalled = 0;
        }

        /* Whatever X2_PEEK names, on EVERY beat -- a spin is a loop over
           state the ring cannot show, and one dump at the stall shows the
           value that is stuck without showing what it was doing before. It
           prints nothing when X2_PEEK is unset. */
        x86_peek_report();

        p_cross = cross; p_scenes = scenes; p_presents = presents;
        p_clears = clears; p_draws = draws;
        p_gpu = gpu_draws; p_ref = gpu_refused;
    }
    return NULL;
}

void heartbeat_start(void)
{
    const char *e = getenv("X2_HEARTBEAT");
    pthread_t th;
    int rc;

    if (e && *e) g_period = strtod(e, NULL);
    if (g_period <= 0.0) {
        fprintf(stderr, "[HB] disabled (X2_HEARTBEAT=%s). A run that stops "
                        "producing output will say nothing about whether it is "
                        "alive.\n", e ? e : "0");
        return;
    }
    g_t0 = now_s();
    g_running = 1;
    rc = pthread_create(&th, NULL, heartbeat_thread, NULL);
    if (rc != 0) {
        fprintf(stderr, "[HB] could not start the heartbeat thread (%s) -- "
                        "this run has NO liveness reporting.\n", strerror(rc));
        g_running = 0;
        return;
    }
    pthread_detach(th);
    fprintf(stderr, "[HB] a liveness line every %.1fs, counting guest %s and "
                    "the D3D8 frame counters (X2_HEARTBEAT=<seconds>, 0 to "
                    "disable).\n", g_period, x86_crossings_what());
}
