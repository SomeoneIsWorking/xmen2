/* See heartbeat.h. */
#include "threads.h"
#include "heartbeat.h"
#include "oracle_trace.h"

#include "x86rt.h"
#include "x86rt_native.h"
#include "d3d8_resource.h"
#include "d3d8_vertex_shader.h"
#include "d3d8_device.h"
#include "gpu_draw.h"
#include "gpu_device.h"

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

static int      g_silent;      /* X2_HEARTBEAT=0: no beat, but still the reporter */
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
        while (slept < g_period && !x2_report_now
               && !gpu_frame_limit_reached()) {
            req.tv_sec = 0;
            req.tv_nsec = 250000000L;
            while (nanosleep(&req, &req) != 0 && errno == EINTR) ;
            slept += 0.25;
        }
        /* A clean stop on the frame counter takes the same path a SIGTERM
           does, so nothing new has to be trusted. */
        /* 2 = a clean stop on the frame counter, 1 = a signal. Both take this
           path; only the second wants the ring. */
        if (!x2_report_now && gpu_frame_limit_reached()) x2_report_now = 2;
        if (x2_report_now) {
            int killed = (x2_report_now == 1);
            fprintf(stderr, killed
                    ? "\n[HB] interrupted -- the shutdown reports follow, "
                      "taken while the guest is STILL RUNNING, so every count "
                      "is a snapshot rather than a final total.\n"
                    : "\n[HB] the frame limit was reached -- the shutdown "
                      "reports follow. The guest is still running, so every "
                      "count is a snapshot rather than a final total.\n");
            fflush(stderr);
            x2_interrupt_reports(killed);
            _exit(killed ? 4 : 0);
        }

        /* Silenced: the thread exists only to carry the shutdown report. */
        if (g_silent) continue;

        t = now_s() - g_t0;
        cross = x86_crossings();
        have_dev = d3d8_device_counts(&scenes, &presents, &clears, &draws);
        gpu_draw_counts(&gpu_draws, &gpu_refused);
        {   /* Multimedia timers: a stall whose cause is "the callback that
               would have ended this wait never ran" looks exactly like any
               other stall until these are on the line. */
            extern void winmm_counts(unsigned long *, unsigned long *, int *);
            static unsigned long p_fire, p_pump;
            unsigned long fire, pump; int live;
            winmm_counts(&fire, &pump, &live);
            if (fire || pump || live)
                fprintf(stderr, "[HB]           winmm %lu fire(s) (+%lu), "
                                "%lu pump(s) (+%lu), %d timer(s) live\n",
                        fire, fire - p_fire, pump, pump - p_pump, live);
            p_fire = fire; p_pump = pump;
        }
        {
            /*
             * Preemptions, HERE and not only at shutdown.
             *
             * Every run of this game is killed by a timeout, and the shutdown
             * report is written from a signal handler that may be cut short --
             * the first measurement of the quantum was lost exactly that way.
             * A counter that can only be read on a clean exit cannot measure a
             * program that never has one.
             */
            static unsigned long p_ps, p_pl;
            unsigned long ps, pl;
            extern void kernel32_pulse_counts(unsigned long *, unsigned long *);
            kernel32_pulse_counts(&ps, &pl);
            if (ps)
                fprintf(stderr, "[HB]           PulseEvent %lu sent (+%lu), "
                                "%lu LOST with no waiter (+%lu)\n",
                        ps, ps - p_ps, pl, pl - p_pl);
            p_ps = ps; p_pl = pl;
        }
        {
            guest_thread_state_report();
        }
        {
            static unsigned long p_q;
            unsigned long q = guest_quantum_count();
            fprintf(stderr, "[HB]           %lu preemption(s) (+%lu) at a "
                            "quantum of %lu crossing(s)\n",
                    q, q - p_q, guest_quantum_size());
            p_q = q;
        }

        {
            /* The raw per-import probe, read over the same interval as the
               crossings delta: WHICH host imports did that interval hammer.
               The ring collapses tight loops, so it could not say; these
               counters count every call and name the import. Fresh snapshot
               once, then per-interval deltas. */
            static unsigned long *snap;
            static const char *mods[5], *syms[5];
            static unsigned long hits[5];
            unsigned int i, n;
            extern unsigned int x86_thunk_count(void);
            extern unsigned int x86_thunk_crossings_sorted(
                unsigned long *, const char **, const char **,
                unsigned long *, unsigned int);
            if (!snap) {
                snap = calloc(x86_thunk_count(), sizeof *snap);
                if (!snap) {
                    fprintf(stderr, "[HB] thunk probe: calloc failed, "
                                    "disabling the import probe\n");
                    goto thunk_probe_disabled;
                }
            }
            n = x86_thunk_crossings_sorted(snap, mods, syms, hits, 5);
            for (i = 0; i < n; i++)
                fprintf(stderr, "[HB]           import %s!%s: %lu call(s)\n",
                        mods[i] ? mods[i] : "?", syms[i] ? syms[i] : "?",
                        hits[i]);
        thunk_probe_disabled:
            ;
        }
        {
            /* The hot guest bodies, decoded from raw dispatch counts. Armed by
               X2_HOTEP=<n>; unarmed, this prints nothing -- a deliberate
               silence, not a missing line, because the probe has zero meaning
               if it never counted. */
            extern unsigned int x86_hotep_sorted(uint32_t *, unsigned long long *,
                                             unsigned long *, unsigned int);
            extern unsigned int x86_hotep_collisions(void);
            uint32_t eps[5];
            unsigned long long nss[5];
            unsigned long hns[5];
            unsigned int i, n = x86_hotep_sorted(eps, nss, hns, 5);
            for (i = 0; i < n; i++) {
                uint32_t ep = eps[i];
                const char *nm = x86_native_name_at(ep);
                X86Module *m = x86_module_for(ep);
                fprintf(stderr, "[HB]           HOT body %s0x%08x (%s%s): "
                                "%.1f ms in %lu dispatch(es)\n",
                        nm ? "" : "unresolved ", ep,
                        nm ? nm : (m ? m->name : "???"),
                        nm || !m ? "" : " +offset",
                        (double)nss[i] * 1e-6, hns[i]);
            }
            if (n && x86_hotep_collisions())
                fprintf(stderr, "[HB]           HOTEP: %u hash collision(s) -- "
                                "new keys refused, probe may miss the top\n",
                        x86_hotep_collisions());
            {
                /* WHERE the interval's wall time went: host import stubs vs
                   guest bodies. This is the number the crossing count cannot
                   give -- a frame with 500k crossings could be slow either
                   way, and only the split says which. Printed at zeroes too:
                   "the probe was unarmed" must not read as "the guest cost
                   nothing". */
                extern void x86_probe_time_delta(unsigned long long *,
                                                 unsigned long long *);
                unsigned long long hn, gn;
                unsigned long long total;
                x86_probe_time_delta(&hn, &gn);
                total = hn + gn;
                if (!total)
                    fprintf(stderr, "[HB]           wall-time split: probe "
                                    "unarmed (X2_HOTEP) -- set it to attribute "
                                    "where frame time goes\n");
                else
                    fprintf(stderr,
                            "[HB]           wall-time split this interval: "
                            "host imports %.1f ms (%.0f%%), guest bodies "
                            "%.1f ms (%.0f%%)\n",
                            (double)hn * 1e-6, 100.0 * (double)hn / total,
                            (double)gn * 1e-6, 100.0 * (double)gn / total);
            }
        }
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
            {
                extern void d3d8_drawcall_multistage(unsigned long *, int *);
                static unsigned long p_ms;
                unsigned long ms; int most;
                d3d8_drawcall_multistage(&ms, &most);
                /* Printed even at ZERO, next to the draw total. "No draw
                   wanted a second stage" is a real finding about this game and
                   is worth as much as a large number; a line that appears only
                   when non-zero cannot be told from a check nobody ran. */
                fprintf(stderr, "[HB]           %lu of %lu draw(s) (+%lu) "
                                "wanted a texture stage beyond 0 (up to %d "
                                "extra), which this backend does not read\n",
                        ms, draws, ms - p_ms, most);
                p_ms = ms;
            }
            {
                extern void d3d8_drawcall_combiner_args(unsigned long *,
                                                        unsigned long *,
                                                        uint32_t[4]);
                unsigned long dflt, other; uint32_t f[4];
                d3d8_drawcall_combiner_args(&dflt, &other, f);
                /* Printed at zero as well: the shader ASSUMES the default
                   arguments, and "no draw disagreed" is the measurement that
                   licenses the assumption. */
                fprintf(stderr, "[HB]           combiner args: %lu default, "
                                "%lu other%s\n", dflt, other,
                        other ? "" : " -- the shader's assumption holds");
                if (other)
                    fprintf(stderr, "[HB]             first non-default: "
                                    "COLORARG1 %u COLORARG2 %u ALPHAARG1 %u "
                                    "ALPHAARG2 %u\n", f[0], f[1], f[2], f[3]);
            }
            fprintf(stderr, "[HB]           gpu draws %lu (+%lu)  refused %lu "
                            "(+%lu)%s\n",
                    gpu_draws, gpu_draws - p_gpu, gpu_refused,
                    gpu_refused - p_ref,
                    gpu_draws == p_gpu && draws != p_draws
                    ? "  -- the engine asked and the BACKEND drew none" : "");
            /*
             * Frame-phase profiling, live.
             *
             * Two reads, one line: the DEVICE's present-to-present wall time
             * and the DRAW side's host share (gpu_draw + uploads). The line
             * the frame is paced to 60 fps at reads zero draw time and the
             * wall time is the vsync wait; an UNPACED gameplay run reads the
             * frame cost and the host's share of it, which is where a hotspot
             * has to show up before anything gets "fixed". Printed at zero as
             * a baseline like everything else here, not only once non-zero.
             */
            {
                unsigned long long fns, fmin, fmax, esub;
                const unsigned long *hist;
                unsigned long long dns, uns, una, unsb, tc;
                unsigned long up, sb, intervals;
                gpu_device_perf(&fns, &fmin, &fmax, &esub, &intervals, &hist);
                gpu_draw_perf(&dns, &uns, &una, &unsb, &tc, &up, &sb);
                fprintf(stderr, "[HB]           perf: frame wall avg %.1f ms "
                                "min %.1f max %.1f (of %lu intervals) -- host "
                                "draw %.2f ms/frame, host upload %.2f "
                                "ms/frame (alloc %.2f + submit %.2f), %lu "
                                "uploads and %lu transfer-buffer alloc(s)\n",
                        fns && intervals ? (double)fns * 1e-6
                                           / (double)intervals : 0.0,
                        fmin ? (double)fmin * 1e-6 : 0.0,
                        (double)fmax * 1e-6,
                        intervals,
                        intervals ? (double)dns * 1e-6 / (double)intervals
                                  : 0.0,
                        intervals ? (double)uns * 1e-6 / (double)intervals
                                  : 0.0,
                        intervals ? (double)una * 1e-6 / (double)intervals
                                  : 0.0,
                        intervals ? (double)unsb * 1e-6 / (double)intervals
                                  : 0.0,
                        (unsigned long)up, (unsigned long)tc);
                (void)esub; (void)sb; (void)hist;
            }
            /*
             * Dynamic buffers, live, and the write-after-read hazard under
             * them.
             *
             * gpu_buffer_upload acquires its OWN command buffer and submits
             * it at once, while the frame's command buffer stays open until
             * Present -- so every mid-frame upload reaches the GPU BEFORE
             * every draw of that frame. That only corrupts a picture if some
             * draw read the buffer earlier in the same frame, which is what
             * the second line counts. The first counter here (relocked in one
             * frame) does NOT answer this: it asks whether a buffer was
             * unlocked twice, and a buffer drawn once and rewritten once is
             * the hazard while never being unlocked twice.
             *
             * Printed AT ZERO with its denominator: zero here means the
             * submission order cannot be what warps the geometry, and that is
             * a result rather than a missing line.
             */
            {
                char vsl[256];
                d3d8_vertex_shader_binding_line(vsl, sizeof vsl);
                fprintf(stderr, "[HB]           %s\n", vsl);
            }
            /* The oracle probes. Live, and printed whether armed or not: a
               capture that recorded nothing must be visible DURING the run,
               not discovered afterwards when the stream is compared and its
               emptiness reads as agreement. */
            {
                char pl[192];
                oracle_probe_line(pl, sizeof pl);
                fprintf(stderr, "[HB]           %s\n", pl);
                d3d8_vsconst_caller_line(pl, sizeof pl);
                fprintf(stderr, "[HB]           %s\n", pl);
            }
            {
                static unsigned long p_unl, p_byt, p_rel, p_haz;
                unsigned long lk, dis, noov, unl, byt, rel, haz;
                d3d8_buffer_lock_counts(&lk, &dis, &noov, &unl, &byt, &rel,
                                        &haz);
                fprintf(stderr, "[HB]           buffer locks %lu (%lu DISCARD, "
                                "%lu NOOVERWRITE); %lu unlock(s) (+%lu) moved "
                                "%lu MB (+%lu MB); %lu (+%lu) relocked in one "
                                "frame\n",
                        lk, dis, noov, unl, unl - p_unl,
                        byt >> 20, (byt - p_byt) >> 20, rel, rel - p_rel);
                fprintf(stderr, "[HB]           of those unlocks, %lu (+%lu) "
                                "REWROTE A BUFFER THIS FRAME'S DRAWS HAD "
                                "ALREADY READ -- and the copy is submitted "
                                "ahead of them\n", haz, haz - p_haz);
                p_unl = unl; p_byt = byt; p_rel = rel; p_haz = haz;
            }
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
    /*
     * X2_HEARTBEAT=0 silences the beat -- it does NOT stop the thread.
     *
     * The end-of-run reports are handed to this thread on purpose (they are
     * stdio, and a signal handler deadlocks against whatever the interrupted
     * code was holding), so returning here took the shutdown report away with
     * the liveness line. Turning down one diagnostic silently removed a
     * different one, and the way that presented was a run that simply printed
     * no report at all -- which reads as the run having produced nothing.
     */
    if (g_period <= 0.0) {
        g_silent = 1;
        fprintf(stderr, "[HB] the liveness line is off (X2_HEARTBEAT=%s). The "
                        "thread still runs, because the END-OF-RUN report is "
                        "printed from it; a run that stops producing output "
                        "will say nothing about whether it is alive.\n",
                e ? e : "0");
        g_period = 5.0;         /* it still has to wake to notice a signal */
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
