/*
 * Native overrides that belong to the MOVIE / MEDIA subsystem.
 *
 * libCriMovie's decoder is a guest thread that parks and is spun by its
 * partner; the native override replaces the spin with a bounded wait. It is
 * registered below against libCriMovie, the module that owns the entry point:
 * every libIG*.dll is linked for 0x10000000, so a bare address would name a
 * function in eight modules and this override was dead until it said which
 * (C212). The recompiled body stays emitted and linked as
 * fn_libCriMovie_10002520, so the two stay diffable.
 */
#include "x86rt.h"
#include "x86rt_native.h"
#include "pe_map.h"
#include "threads.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------------
 * libCriMovie 0x10002520 -- the movie decoder's spin partner (issue #57).
 *
 * GUEST PROTOCOL, read off the disassembly, not inferred: across a global
 * lock, the decoder (libCriMovie 0x10002630) and this partner rendezvous on
 * the flag at libCriMovie+0x572b0. The partner
 *
 *     1000252f  MOV [0x100572b0], 1        -- arm the flag
 *     1000255d  CMP ESI, 0x2dc6c0          -- then, up to 3,000,000 times:
 *        SetThreadPriority(decoder, [0x10057294])   via EDI = [0x10042070]
 *        ResumeThread(decoder)                      via EBX = [0x1004206c]
 *        ... until the flag clears; ESI counts the retries, and ESI == 0x2dc6c0
 *        on the way out is the out-of-patience error (FUN_10008370).
 *     1000257a  SetThreadPriority(decoder, [0x100572a8])  -- restore
 *     RET 4+0, EAX = that SetThreadPriority call's return (1 in this port).
 *
 * The decoder clears the flag to 0 and parks -- SuspendThread on ITSELF via
 * [0x10042058], the park path at 0x1000266d -- at its very next loop top
 * whenever the flag is 1, whether or not it has work to do. The flag being 1
 * is the signal "park now so I can see you caught up", and the spin is a poll
 * for the park. It is NOT a general run-to-park on every resume: it is armed
 * explicitly, before we wait, and the arm is exactly what guarantees the
 * decoder parks instead of continuing.
 *
 * SO THE SPIN IS REPLACED, not just yielded on: arm the flag, resume the
 * decoder once the way a single spin iteration would, then BLOCK until the
 * flag clears. One resume is enough because the decoder, once runnable, clears
 * the flag at its loop top by the same logic the spin polled for. If it has
 * not parked within a generous bound, DEFER to the retained body, which spins
 * exactly as today -- the override cannot make the load window worse, only
 * better, and "deferred" is logged when that happens, never silent.
 */
void fn_libCriMovie_10002520(CPU *C);

#define LCR_FLAG       0x572b0u   /* 1 = "park now"; the decoder zeroes it        */
#define LCR_SPIN_PRIO  0x57294u   /* the priority the spin raises the decoder to */
#define LCR_REST_PRIO  0x572a8u   /* the priority restored before returning      */
#define LCR_DECODER    0x14a1fcu  /* the decoder thread's own handle             */
#define LCR_MAX_WAITS  1000u      /* 1 ms waits; on expiry, defer to the spin    */

void x2_override_10002520(CPU *C)
{
    static int mode = -1;                     /* -1 unknown, 0 wait, 1 spin */
    static int said;
    const X86Module *m;
    uint32_t base = 0;
    uint32_t handle;
    unsigned long waits;
    int deferred;

    if (mode < 0) {
        const char *e = getenv("X2_SPIN");
        mode = (e && *e && *e != '0' && !strcmp(e, "spin")) ? 1 : 0;
    }
    if (mode) {                              /* the CONTROL: run as shipped */
        fn_libCriMovie_10002520(C);
        return;
    }

    for (m = x86_modules(); m; m = m->next)
        if (!strcmp(m->name, "libCriMovie.dll")) { base = *m->base; break; }
    if (!base) {
        fprintf(stderr, "override: libCriMovie 0x10002520 cannot find the "
                        "module; deferring the spin to the original body.\n");
        fn_libCriMovie_10002520(C);
        return;
    }

    if (!said++)
        printf("override: libCriMovie 0x10002520, the decoder rendezvous spin, "
               "is WAITED FOR rather than spun (issue #57).\n"
               "  Set X2_SPIN=spin to run the original 3,000,000-iteration "
               "resume loop instead -- the control this treatment is judged "
               "against.\n");

    WR32(base + LCR_FLAG, 1u);                       /* arm, as 0x1000252f */
    guest_thread_priority_set((int32_t)RD32(base + LCR_SPIN_PRIO)); /* the spin's raise */
    handle = RD32(base + LCR_DECODER);
    guest_thread_resume(handle);                     /* one spin iteration */

    for (waits = 0; waits < LCR_MAX_WAITS && RD32(base + LCR_FLAG) == 1u; waits++)
        guest_cond_wait_ms(1);                       /* give the decoder CPU */

    deferred = (RD32(base + LCR_FLAG) == 1u);
    if (deferred) {
        fprintf(stderr, "override: libCriMovie 0x10002520: the decoder did not "
                        "clear the flag within %lu ms of being resumed. "
                        "DEFERRING to the original spin, which is the faithful "
                        "behaviour the override exists to avoid -- logged so "
                        "the fallback is never silent.\n", waits);
        fn_libCriMovie_10002520(C);           /* spins as today; sets EAX */
        return;
    }

    guest_thread_priority_set((int32_t)RD32(base + LCR_REST_PRIO)); /* restore */
    C->eax = 1u;      /* the port's SetThreadPriority returns 1; same as the body */
    C->esp += 4u;     /* RET: the body pops only its own return address */
}

__attribute__((constructor))
static void x2_movie_register_overrides(void)
{
    x86_register_override("libCriMovie.dll", 0x10002520, x2_override_10002520);
}
