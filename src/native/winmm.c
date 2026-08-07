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

#include <stdio.h>

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

/*
 * timeSetEvent / timeKillEvent -- NOT implemented, and they say so by name.
 *
 * timeSetEvent(delay, resolution, callback, user, flags) calls back into GUEST
 * code from a timer thread. This runtime executes recompiled bodies on one
 * thread and its CPU state is not shared-safe, so a second thread entering a
 * body is a data race on the register file itself.
 *
 * The two dishonest options are both worse than stopping. Returning a fake
 * timer id means the callback never fires and whatever it drives simply never
 * happens -- silently, and the caller believes it has a timer. Returning 0
 * (failure) is truthful but sends libCriMovie down an error path for a reason
 * that has nothing to do with the movie.
 *
 * So they abort by name, which is this port's standard answer for an
 * unimplemented import and puts the work item where it can be seen.
 */
void imp_WINMM_timeSetEvent(CPU *C)
{
    (void)C;
    x86_missing_import("WINMM.dll", "timeSetEvent");
}

void imp_WINMM_timeKillEvent(CPU *C)
{
    (void)C;
    x86_missing_import("WINMM.dll", "timeKillEvent");
}
