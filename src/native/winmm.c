/*
 * WINMM -- the four multimedia-timer entry points libCriMovie imports.
 *
 * Two of them are implementable here and two are not, and the split is the
 * point: timeBeginPeriod/timeEndPeriod ASK for a scheduling resolution, and
 * timeSetEvent/timeKillEvent RUN GUEST CODE on a timer thread. The first pair
 * has an honest answer on this host; the second would need a second thread
 * executing recompiled bodies, which this runtime does not have, and there is
 * no way to fake a callback that never fires.
 */
#include "x86rt.h"
#include "x86rt_native.h"
#include "winmm.h"
#include "igvk_ark.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#define A(i)  RD32(C->esp + 4u + (uint32_t)(i) * 4u)

#define TIMERR_NOERROR  0u

static void ret_std(CPU *C, uint32_t eax, int nargs)
{
    C->eax = eax;
    C->esp += 4u + (uint32_t)nargs * 4u;
}

/*
 * timeBeginPeriod(uPeriod) / timeEndPeriod(uPeriod)
 *
 * On Windows these raise and lower the SYSTEM timer resolution, because the
 * default there is ~15.6 ms and a media player needs better. Linux's timers
 * are already at nanosecond resolution and nothing here is throttled by a
 * scheduler tick, so the request is granted by being unnecessary -- which is
 * exactly what TIMERR_NOERROR means to the caller.
 *
 * That is a real answer, not a stub: the caller asked for a guarantee this
 * host already provides. Returning the error code instead would make a player
 * that checks it conclude it cannot keep time.
 */
static void imp_WINMM_timeBeginPeriod_(CPU *C)
{
    static int said;
    if (!said++)
        printf("winmm: timeBeginPeriod(%u ms) -- granted. This host's timers "
               "are nanosecond-resolution already, so there is nothing to "
               "raise.\n", A(0));
    ret_std(C, TIMERR_NOERROR, 1);
}

static void imp_WINMM_timeEndPeriod_(CPU *C) { ret_std(C, TIMERR_NOERROR, 1); }

void imp_WINMM_timeBeginPeriod(CPU *C) { imp_WINMM_timeBeginPeriod_(C); }
void imp_WINMM_timeEndPeriod(CPU *C)   { imp_WINMM_timeEndPeriod_(C); }

/* ---- the multimedia timers --------------------------------------------
 *
 * timeSetEvent(delay, resolution, proc, user, flags) runs a GUEST callback on
 * a timer thread. This runtime executes recompiled bodies on ONE thread and
 * the CPU state is a plain struct passed down by pointer, so a second thread
 * entering a body would race the register file itself. A real timer thread is
 * therefore not a five-line implementation -- it is a threading model, and it
 * is issue #42's option 2.
 *
 * This is option 1: the callbacks are DEFERRED and run on the guest's own
 * thread, from the pump below, which the host calls at points the guest
 * reaches anyway (asking the time, sleeping). No thread, no race.
 *
 * WHAT IS DIFFERENT FROM WINDOWS, said out loud because it is the whole
 * trade: the resolution is the POLL INTERVAL, not the millisecond that was
 * asked for. A callback is late by however long the guest goes without
 * reaching a pump point, and one that the guest never reaches at all never
 * fires. That is reported at exit rather than left to be discovered.
 *
 * The two shortcuts this replaces were both worse than stopping: a fake timer
 * id means the callback never fires while the caller believes it has a timer,
 * and returning failure sends libCriMovie down an error path for a reason that
 * has nothing to do with the movie.
 */
#define MAX_TIMERS 16
#define TIME_ONESHOT  0x0000u
#define TIME_PERIODIC 0x0001u
#define TIME_CALLBACK_TYPE_MASK 0x0030u   /* 0 = function, 0x10/0x20 = event */

static struct {
    int      used, periodic;
    uint32_t proc, user, delay_ms;
    double   due;
    unsigned long fired;
} g_timer[MAX_TIMERS];
static unsigned long g_pumps, g_fires, g_late_ms_total;
static int g_pumping;

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

void imp_WINMM_timeSetEvent(CPU *C)
{
    uint32_t delay = A(0), proc = A(2), user = A(3), flags = A(4);
    int i;

    if ((flags & TIME_CALLBACK_TYPE_MASK) != 0u) {
        /* TIME_CALLBACK_EVENT_SET/PULSE signal an EVENT rather than calling a
           function. Refused by name rather than treated as a function
           callback, which would call whatever the handle happens to be. */
        fprintf(stderr, "winmm: timeSetEvent with callback type 0x%x -- this "
                        "host implements the FUNCTION callback only; the event "
                        "forms would call a handle as if it were code.\n",
                flags & TIME_CALLBACK_TYPE_MASK);
        ret_std(C, 0, 5);
        return;
    }
    if (!proc) { ret_std(C, 0, 5); return; }
    for (i = 0; i < MAX_TIMERS; i++) if (!g_timer[i].used) break;
    if (i == MAX_TIMERS) {
        fprintf(stderr, "winmm: all %d timer slots are live; timeSetEvent "
                        "fails, which is what Windows does when the system is "
                        "out of timers.\n", MAX_TIMERS);
        ret_std(C, 0, 5);
        return;
    }
    memset(&g_timer[i], 0, sizeof g_timer[i]);
    g_timer[i].used = 1;
    g_timer[i].periodic = (flags & TIME_PERIODIC) != 0;
    g_timer[i].proc = proc;
    g_timer[i].user = user;
    g_timer[i].delay_ms = delay ? delay : 1u;
    g_timer[i].due = now_s() + (double)g_timer[i].delay_ms / 1000.0;
    {
        static int said;
        if (!said++)
            printf("winmm: timeSetEvent(%u ms, %s) -- the callback runs on the "
                   "GUEST's thread from the next pump point (a clock read or a "
                   "sleep), not on a timer thread.\n"
                   "  So its resolution is the poll interval, not %u ms. See "
                   "issue #42; the lateness is reported at exit.\n",
                   delay, g_timer[i].periodic ? "periodic" : "one-shot", delay);
    }
    ret_std(C, (uint32_t)(i + 1), 5);
}

void imp_WINMM_timeKillEvent(CPU *C)
{
    uint32_t id = A(0);
    if (id == 0 || id > MAX_TIMERS || !g_timer[id - 1].used) {
        ret_std(C, 97u, 1);                    /* MMSYSERR_INVALPARAM */
        return;
    }
    g_timer[id - 1].used = 0;
    ret_std(C, TIMERR_NOERROR, 1);
}

/*
 * Run whatever is due, on the caller's thread.
 *
 * Re-entrancy is guarded rather than assumed: the callback is guest code, and
 * guest code asks the time -- which is one of the places this is called from.
 */
void winmm_timers_pump(void)
{
    double t;
    int i;

    if (g_pumping) return;
    g_pumps++;
    t = now_s();
    for (i = 0; i < MAX_TIMERS; i++) {
        if (!g_timer[i].used || t < g_timer[i].due) continue;
        {
            /* void CALLBACK TimeProc(UINT id, UINT msg, DWORD user,
                                      DWORD dw1, DWORD dw2) */
            uint32_t args[5];
            double late = t - g_timer[i].due;
            args[0] = (uint32_t)(i + 1);
            args[1] = 0;
            args[2] = g_timer[i].user;
            args[3] = 0;
            args[4] = 0;
            g_late_ms_total += (unsigned long)(late * 1000.0);
            if (g_timer[i].periodic)
                g_timer[i].due = t + (double)g_timer[i].delay_ms / 1000.0;
            else
                g_timer[i].used = 0;
            g_timer[i].fired++;
            g_fires++;
            g_pumping = 1;
            ark_call_cdecl(g_timer[i].proc, args, 5);
            g_pumping = 0;
        }
    }
}

void winmm_report(void)
{
    int i, live = 0, ever = 0;
    for (i = 0; i < MAX_TIMERS; i++) {
        if (g_timer[i].used) live++;
        if (g_timer[i].fired) ever++;
    }
    if (!g_pumps && !g_fires && !live) {
        printf("  winmm: no multimedia timer was ever set.\n");
        return;
    }
    printf("  winmm: %lu timer callback(s) run from %lu pump(s), %d timer(s) "
           "still live\n", g_fires, g_pumps, live);
    if (g_fires)
        printf("         average lateness %lu ms -- these run at the POLL "
               "interval, not at the interval the guest asked for (issue #42)\n",
               g_late_ms_total / g_fires);
    for (i = 0; i < MAX_TIMERS; i++)
        if (g_timer[i].used && !g_timer[i].fired)
            printf("         timer %d (%u ms) has NEVER fired -- the guest has "
                   "not reached a pump point since it was set, so whatever it "
                   "drives is not happening\n", i + 1, g_timer[i].delay_ms);
}
