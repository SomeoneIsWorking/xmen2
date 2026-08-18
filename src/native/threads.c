/*
 * Guest threads, as COROUTINES on one host thread.
 *
 * libCriMovie asks for a decoding thread before it will play the intro movie
 * (issue #43), and every subsystem after it -- streaming sound, background
 * loading -- asks for the same thing. So this is the general answer rather
 * than a way around one caller.
 *
 * WHY ONE HOST THREAD. Exactly one guest thread has always executed at a time
 * here: the kernel32 handle table, the guest heap's free lists, the D3D8
 * object tables, the boundary ring and the CRT's statics are all
 * single-threaded by an assumption nobody wrote down, and auditing that at
 * once is how a threading change becomes a month of heisenbugs. The old
 * implementation got the invariant with a pthread per guest thread and a
 * global mutex -- which meant the HOST scheduler decided which guest thread
 * ran next, and `sched_yield` decided when. That is where the intermittency
 * came from: the same run took a different schedule every time, and issue #57
 * spent three sessions producing wrong attributions because of it.
 *
 * So the threads are ucontext coroutines and the schedule is OURS:
 *
 *   - one host thread runs every guest thread, in turn;
 *   - a switch happens only at points named in this file;
 *   - the next thread is picked round-robin, so the same run makes the same
 *     choices on every machine.
 *
 * Nothing about the one-at-a-time invariant changed, so everything built on it
 * is exactly as safe as it was. What changed is that the schedule is now a
 * property of this program rather than of the host's scheduler, and a stall is
 * reproducible instead of being a coin toss.
 *
 * PREEMPTION IS STILL REQUIRED, and this is why. libCriMovie's rendezvous, read
 * out of the guest (issue #57 has the addresses): the decoder loop works until
 * it runs dry and only then parks itself with SuspendThread; its partner sets a
 * flag and SPINS up to 3,000,000 times calling ResumeThread until the decoder
 * clears it. BOTH sides spin and neither blocks. Two hand-off designs were
 * measured and both made it worse. What schedules two spinners is preemption
 * neither side has to cooperate with: guest_quantum(), fired from X86_ENTER_FN
 * in EVERY recompiled body every N of them. Not from the dispatch boundary --
 * that only sees dispatched calls, and this decoder spins on direct
 * guest-to-guest calls that never reach it (measured; see x86rt.h).
 *
 * PER-THREAD STATE that is not the register file. The register file is a plain
 * struct on the C stack, so it comes free with the coroutine's own stack. The
 * rest has to be SWITCHED explicitly, because a coroutine cannot use __thread
 * for it -- every guest thread is the same host thread now, so a __thread
 * variable would be one variable shared by all of them:
 *   - FS/GS base. FS:[0] is the SEH chain head and every recompiled prologue
 *     writes it; sharing it would have two threads' exception chains
 *     overwriting each other. Each thread gets its own TIB page.
 *   - The TLS slots. TlsGetValue/TlsSetValue are per-thread BY DEFINITION.
 *     kernel32 keeps one array per slot and this file selects it.
 *   - The guest stack, out of the guest arena, and the HOST stack the
 *     recompiled C runs on.
 */
#include "threads.h"
#include "guest_clock.h"

#include "x86rt.h"
#include "x86rt_native.h"
#include "guest_heap.h"
#include "pe_map.h"
#include "winmm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>
#include <sys/mman.h>

/* kernel32 owns the handle table and the TLS arrays; these are the hooks. */
uint32_t k32_handle_for_thread(void *rec);
void    *k32_thread_record(uint32_t handle);
unsigned k32_thread_handle_count(void *rec);
void     k32_handle_thread_done(void *rec);
void     k32_tls_switch(int slot);
uint32_t k32_tls_peek(int slot, uint32_t index);
int      k32_tls_slot_count(void);

/* ---- what each guest thread is doing ------------------------------------ */

enum { TS_NEW = 0, TS_RUNNING, TS_LOCK, TS_COND, TS_BLOCKING, TS_SUSPENDED,
       TS_DONE };
static const char *const TS_NAME[] = {
    "new", "running guest code", "runnable, waiting its turn",
    "in a WAIT (condition variable)", "in a blocking host call",
    "SUSPENDED", "finished"
};

#define MAX_THREADS   16
#define MAIN_SLOT     MAX_THREADS        /* the main thread's TLS slot */
/* kernel32.c has to know it too, and before this file gets to run. */
#if MAIN_SLOT != GUEST_MAIN_TLS_SLOT
#error "MAIN_SLOT and GUEST_MAIN_TLS_SLOT disagree; kernel32 would give the main thread the wrong TLS"
#endif
#define TIB_BYTES     0x1000u
#define STACK_DEFAULT (256u * 1024u)

/*
 * The HOST stack a guest thread's recompiled C runs on.
 *
 * Nothing to do with the guest's own stack: this is the C stack for the
 * translated bodies, which nest as deeply as the guest's call graph does. 8 MB
 * is what the host gives its main thread, and it is reserved rather than
 * committed (MAP_NORESERVE), so sixteen of them cost address space and not
 * memory. A PROT_NONE page at the low end turns an overflow into a fault at
 * the point of overflow instead of into corruption of whatever is below.
 */
#define HSTACK_BYTES  (8u * 1024u * 1024u)
#define HSTACK_GUARD  0x1000u

typedef struct {
    int       used, finished, suspended;
    int       slot;              /* index in g_thread; also the TLS slot */
    int       is_main;
    uint32_t  handle;            /* the kernel32 handle the guest holds */
    uint32_t  tid;
    uint32_t  start, arg;
    uint32_t  stack_base, stack_bytes;
    uint32_t  tib;
    uint32_t  exit_code;
    /* Per-thread, because the totals were misleading in exactly the way that
       matters: a run with 3,000,045 resumes and 43 suspends reads as a wildly
       active suspend/resume protocol, and is in fact one thread being resumed
       in a spin while ANOTHER sits parked and is never named. */
    unsigned long n_suspend, n_resume, n_ran;
    int       reaped;            /* its handle was closed and its memory freed */
    /*
     * WHAT THIS THREAD IS DOING RIGHT NOW, and since when.
     *
     * Three mechanisms were proposed for issue #57's intermittent stall and
     * all three were guesses -- a hand-off, a quantum, a lost pulse -- because
     * nothing here could answer "what is the other thread blocked ON?". The
     * totals could not: a thread parked in a condition wait and a thread
     * spinning in guest code both show up as "1 still running".
     */
    int          state;
    double       state_since;

    /* ---- the coroutine ---- */
    ucontext_t   ctx;
    void        *hstack;
    int          waiting;        /* in guest_cond_wait_ms */
    double       wake_at;        /* 0 = no deadline (INFINITE) */
    int          depth;          /* guest_lock nesting, for guest_quantum */
    uint32_t     fsbase, gsbase; /* saved across a switch */
    int32_t      priority;       /* SetThreadPriority, per thread */
} GuestThread;

static GuestThread  g_thread[MAX_THREADS + 1];   /* +1: the main thread */
static GuestThread *g_self;                      /* the one running NOW */
static ucontext_t   g_sched_ctx;
static char         g_sched_stack[64 * 1024];
static int          g_sched_ready;
static int          g_rr;                        /* round-robin cursor */

/* Declared in x86rt.h and __thread there: they are switched by hand below,
   because on one host thread __thread does not separate guest threads. */
extern __thread uint32_t g_fsbase, g_gsbase;

/* The guest's clock, not a private one: see guest_clock.h. Five copies of
   this read CLOCK_MONOTONIC directly, and the guest gates real logic on
   elapsed time, so any two of them disagreeing is a timing bug wearing a
   gameplay bug's clothes. */
static double now_s(void) { return guest_clock_now_s(); }

static void state_set(int st)
{
    if (!g_self) return;
    if (g_self->state == st) return;
    g_self->state = st;
    g_self->state_since = now_s();
}

/*
 * WHICH guest thread is running, in the guest's own numbering.
 *
 * GetCurrentThreadId used to return the HOST thread id, which was harmless
 * only for as long as nothing compared two of them. Critical sections do
 * exactly that -- an owner field is a thread id -- and now that every guest
 * thread IS the same host thread, gettid() would give them all the same
 * answer. This is the number that separates them.
 */
#define MAIN_TID 999u
static void sched_attach_main(void);

uint32_t guest_current_tid(void)
{
    return g_self ? g_self->tid : MAIN_TID;
}

void *guest_thread_current_record(void)
{
    if (!g_self) sched_attach_main();
    return g_self;
}

/*
 * SetThreadPriority / GetThreadPriority, per thread.
 *
 * Recorded and round-tripped, and that is ALL: the schedule here is
 * round-robin and a priority cannot change it. It is per-thread rather than
 * one global because Get must return what THIS thread set -- libCriMovie's
 * decoder saves its priority, raises it, and restores it, and a shared global
 * would hand it back whatever another thread had set in between.
 */
void guest_thread_priority_set(int32_t p)
{
    if (!g_self) sched_attach_main();
    g_self->priority = p;
}

int32_t guest_thread_priority_get(void)
{
    return g_self ? g_self->priority : 0;
}

/* ---- the scheduler ------------------------------------------------------ */

static unsigned long g_switches;         /* coroutine switches performed */
static unsigned long g_quanta;           /* preemptions actually taken */
static unsigned long g_idle_spins;       /* scheduler passes with nobody to run */
static unsigned long g_quantum = 20000;
static unsigned long g_created, g_exited, g_suspends, g_resumes, g_reaped;
static unsigned long g_resume_unknown, g_suspend_unknown, g_resume_noop;
static uint32_t g_next_tid = 1000;

static int runnable(const GuestThread *t)
{
    return t->used && !t->finished && !t->suspended && !t->waiting;
}

/* How many OTHER threads could run right now. guest_quantum uses it: a
   preemption with nobody to preempt to is pure cost. */
static int others_runnable(void)
{
    int i, n = 0;
    for (i = 0; i <= MAX_THREADS; i++)
        if (&g_thread[i] != g_self && runnable(&g_thread[i])) n++;
    return n;
}

/* A timed wait whose deadline has passed becomes runnable again. Returns the
   earliest deadline still outstanding, or 0 if none is. */
static double expire_waits(double now)
{
    int i;
    double next = 0.0;
    for (i = 0; i <= MAX_THREADS; i++) {
        GuestThread *t = &g_thread[i];
        if (!t->used || t->finished || !t->waiting || t->wake_at == 0.0) continue;
        if (now >= t->wake_at) { t->waiting = 0; t->wake_at = 0.0; continue; }
        if (next == 0.0 || t->wake_at < next) next = t->wake_at;
    }
    return next;
}

static void sched_loop(void);

/* Give up the host thread. Returns when the scheduler picks this thread
   again. */
static void sched_switch(void)
{
    GuestThread *me = g_self;
    me->fsbase = g_fsbase;
    me->gsbase = g_gsbase;
    g_switches++;
    swapcontext(&me->ctx, &g_sched_ctx);
}

/* Make `t` the running thread. Called only from the scheduler context. */
static void sched_run(GuestThread *t)
{
    g_self   = t;
    g_fsbase = t->fsbase;
    g_gsbase = t->gsbase;
    k32_tls_switch(t->slot);
    t->n_ran++;
    if (t->state == TS_LOCK) state_set(TS_RUNNING);
    swapcontext(&g_sched_ctx, &t->ctx);
}

/*
 * The scheduler.
 *
 * Round-robin from wherever it last stopped, so a thread that yields cannot
 * immediately be picked again while another is ready -- the schedule is a
 * rotation, not a preference. Deterministic on purpose: given the same
 * sequence of yields it makes the same sequence of choices, on every machine
 * and every run, which is what makes a stall reproducible.
 *
 * When NOTHING is runnable it does not spin the CPU: it pumps the multimedia
 * timers (whose callbacks are what a sleeping guest is usually waiting for)
 * and sleeps to the earliest deadline. With no deadline at all every guest
 * thread is in an INFINITE wait or suspended, which is a real deadlock -- and
 * it is REPORTED, repeatedly and with every thread's state, rather than
 * becoming a silent hang that a harness timeout eventually ends.
 */
static void sched_loop(void)
{
    double stuck_since = 0.0, last_report = 0.0;
    for (;;) {
        double now = now_s(), next;
        int i, picked = 0;

        next = expire_waits(now);
        for (i = 1; i <= MAX_THREADS + 1; i++) {
            GuestThread *t = &g_thread[(g_rr + i) % (MAX_THREADS + 1)];
            if (!runnable(t)) continue;
            g_rr = t->slot;
            picked = 1;
            stuck_since = 0.0;
            sched_run(t);
            break;
        }
        if (picked) continue;

        /* Nobody to run. */
        g_idle_spins++;
        winmm_timers_pump();
        if (next != 0.0) {
            double dt;
            /* Nothing is runnable and the earliest thing anyone waits for is
               at `next`, so this interval is defined to contain no guest work.
               Unbounded mode jumps the clock over it instead of sleeping
               through it -- the same code then runs in the same order seeing
               the same timestamps, minus the real seconds nobody used. */
            if (guest_clock_skip_idle_to(next)) continue;
            dt = next - now_s();
            if (dt > 0.0) usleep((useconds_t)((dt > 0.002 ? 0.002 : dt) * 1e6));
            continue;
        }
        if (stuck_since == 0.0) { stuck_since = now; last_report = 0.0; }
        if (now - stuck_since > 5.0 && now - last_report > 15.0) {
            last_report = now;
            fprintf(stderr,
                "threads: DEADLOCK -- for %.0fs not one guest thread has been "
                "runnable. Every one is suspended or in a wait with no "
                "deadline, so nothing can wake anything. What each is doing:\n",
                now - stuck_since);
            guest_thread_state_report();
            fflush(stderr);
        }
        usleep(1000);
    }
}

/*
 * The main thread joins the schedule.
 *
 * It is a guest thread like the others -- it runs guest code, it waits, it can
 * be preempted -- so it gets a slot rather than being a special case that
 * every check has to remember. It has no coroutine stack of its own because it
 * is already running on the host's.
 */
static void sched_attach_main(void)
{
    GuestThread *t = &g_thread[MAIN_SLOT];
    if (t->used) return;
    memset(t, 0, sizeof *t);
    t->used = 1;
    t->is_main = 1;
    t->slot = MAIN_SLOT;
    t->tid = MAIN_TID;
    t->state = TS_RUNNING;
    t->state_since = now_s();
    g_self = t;
    g_rr = MAIN_SLOT;
    if (!g_sched_ready) {
        getcontext(&g_sched_ctx);
        g_sched_ctx.uc_stack.ss_sp   = g_sched_stack;
        g_sched_ctx.uc_stack.ss_size = sizeof g_sched_stack;
        g_sched_ctx.uc_link          = NULL;
        makecontext(&g_sched_ctx, sched_loop, 0);
        g_sched_ready = 1;
    }
}

/*
 * guest_lock / guest_unlock: BOOKKEEPING, not a mutex.
 *
 * There is nothing to lock any more -- one host thread runs every guest thread
 * and a switch happens only where this file says. The depth is still counted
 * because guest_quantum needs it: preempting from a nested hold would return
 * to a caller that believed it was still the running thread.
 *
 * The names are kept because they mark exactly the right places, and every
 * call site's reasoning ("do not let another guest thread in here") is still
 * the reasoning that applies.
 */
void guest_lock(void)
{
    if (!g_self) sched_attach_main();
    g_self->depth++;
    state_set(TS_RUNNING);
}

void guest_unlock(void)
{
    if (g_self && g_self->depth > 0) g_self->depth--;
}

/*
 * PREEMPTION BY QUANTUM.
 *
 * A model that switches only at named syscalls is enough for a guest thread
 * that BLOCKS and useless for one that SPINS -- and libCriMovie's movie
 * rendezvous has BOTH sides spinning (issue #57 has the guest addresses and
 * the two failed hand-off designs). So the running thread gives up its turn
 * every `quantum` boundary crossings whether it cooperates or not.
 *
 * Not from a nested hold, and not when nobody else could run: the first would
 * surprise a caller, the second is pure cost.
 */
void guest_quantum(void)
{
    if (!g_self || g_self->depth != 1) return;
    if (!others_runnable()) return;
    g_quanta++;
    state_set(TS_LOCK);                  /* runnable, waiting its turn */
    sched_switch();
    state_set(TS_RUNNING);
}

void guest_quantum_configure(unsigned long crossings)
{
    g_quantum = crossings;
}

/*
 * X2_QUANTUM: boundary crossings between preemptions. 0 disables it, which is
 * the CONTROL -- a scheduling change has to be measured against a build where
 * the mechanism is off, or "it stopped happening" is not evidence.
 */
void guest_quantum_from_env(void)
{
    const char *e = getenv("X2_QUANTUM");
    char *end;
    unsigned long v;
    if (!e || !*e) return;
    v = strtoul(e, &end, 0);
    if (*end) {
        fprintf(stderr, "threads: X2_QUANTUM=%s is not a number; the default "
                        "of %lu crossings is unchanged.\n", e, g_quantum);
        return;
    }
    if (!v) {
        g_quantum = 0u - 1ul;      /* effectively never */
        printf("threads: X2_QUANTUM=0 -- preemption DISABLED. Two guest "
               "threads that both spin cannot take turns; this is the control "
               "for issue #57, not a configuration to run in.\n");
        return;
    }
    g_quantum = v;
    printf("threads: preemption quantum set to %lu boundary crossing(s).\n",
           g_quantum);
}

unsigned long guest_quantum_size(void)   { return g_quantum; }
unsigned long guest_quantum_count(void)  { return g_quanta; }

static void guest_suspend_point(void);

/*
 * A blocking HOST call -- one this host makes on the guest's behalf that is
 * not a guest wait.
 *
 * Under the coroutine model this can no longer let another guest thread run,
 * and saying so is the point of the comment: a host call that blocks blocks
 * EVERYTHING. The state is still marked, so the heartbeat can name it, and
 * Sleep -- the only caller there was -- now goes through guest_sleep_ms, which
 * yields properly instead of stopping the world for its duration.
 */
void guest_blocking_begin(void) { state_set(TS_BLOCKING); }
void guest_blocking_end(void)
{
    state_set(TS_RUNNING);
    /* A thread that was SUSPENDED while it sat in a wait must not carry on
       executing guest code just because the wait ended. This is the one place
       every blocked thread comes back through, which is why the check lives
       here rather than at each caller. */
    guest_suspend_point();
}

/*
 * The wait every guest wait goes through: park this thread, let the scheduler
 * pick another, come back when something broadcasts or the deadline passes.
 *
 * One wait queue rather than one per object: waits here are rare and a
 * spurious wake costs a re-check, while a queue per handle would have to be
 * created and destroyed with the handle.
 */
void guest_cond_wait_ms(uint32_t ms)
{
    GuestThread *t = g_self;
    if (!t) { sched_attach_main(); t = g_self; }
    t->waiting = 1;
    t->wake_at = (ms == 0xFFFFFFFFu) ? 0.0 : now_s() + (double)ms / 1000.0;
    state_set(TS_COND);
    sched_switch();
    t->waiting = 0;
    t->wake_at = 0.0;
    state_set(TS_RUNNING);
}

/* Wake everything waiting. Called whenever a guest-visible object is
   signalled -- a thread exiting, an event set, a critical section left. Does
   NOT switch: the caller is in the middle of guest code and Win32's SetEvent
   does not yield either. */
void guest_cond_broadcast(void)
{
    int i;
    for (i = 0; i <= MAX_THREADS; i++)
        if (g_thread[i].used && g_thread[i].waiting) {
            g_thread[i].waiting = 0;
            g_thread[i].wake_at = 0.0;
        }
}

/*
 * Sleep.
 *
 * Sleep(0) is Win32's "give up the rest of my turn" and becomes exactly that.
 * Anything longer parks with a deadline, so the other guest threads run for
 * its duration instead of the whole process stopping -- which is what a
 * usleep() here used to do.
 */
void guest_sleep_ms(uint32_t ms)
{
    if (!g_self) sched_attach_main();
    if (ms == 0) {
        if (!others_runnable()) return;
        state_set(TS_LOCK);
        sched_switch();
        state_set(TS_RUNNING);
        return;
    }
    guest_cond_wait_ms(ms);
    guest_suspend_point();
}

/*
 * One line per live guest thread: what it is doing and for how long.
 *
 * Printed from the heartbeat, because a stall is a thing you watch happen --
 * a report at shutdown arrives after a SIGKILL has already ended the argument.
 * The denominator is printed too: a run whose threads are all "running guest
 * code" is a different claim from a run that has no threads to report.
 */
void guest_thread_state_report(void)
{
    /* A duration that keeps reading ~0.0s is not a thread that just changed
       state -- it is a thread that keeps WAKING, which is the difference
       between a poll loop and a park and is the thing worth seeing. */
    double t = now_s();
    int i, live = 0;
    for (i = 0; i <= MAX_THREADS; i++) {
        GuestThread *g = &g_thread[i];
        if (!g->used || g->finished) continue;
        live++;
        fprintf(stderr, "[HB]           %s%u start 0x%08x: %s for %.1fs%s\n",
                g->is_main ? "MAIN tid " : "tid ", g->tid, g->start,
                TS_NAME[g->state < 0 || g->state > TS_DONE ? 0 : g->state],
                t - g->state_since,
                g == g_self ? "  <- running" : "");
    }
    if (!live)
        fprintf(stderr, "[HB]           no live guest thread at all, not even "
                        "the main one -- which cannot happen while this line "
                        "is being printed, so the table is wrong\n");
}

/* ---- creating and ending threads ---------------------------------------- */

static void thread_trampoline(void)
{
    GuestThread *t = g_self;
    CPU C;

    state_set(TS_RUNNING);
    /* Its own TIB, so this thread's SEH chain is its own. The sentinel is
       Win32's end-of-chain marker; a zero would look like a record at 0 to
       anything that walked it. */
    g_fsbase = t->tib;
    *(volatile uint32_t *)(uintptr_t)t->tib = 0xFFFFFFFFu;

    cpu_reset(&C);
    /*
     * The argument sits at [ESP], and NOTHING is pushed above it.
     *
     * x86_guest_call pushes the return address itself -- that is the whole
     * point of it existing (see x86rt_native.c) -- so pushing a second one
     * here put the argument at [ESP+8] instead of [ESP+4], which is where a
     * `DWORD WINAPI proc(void *)` reads it from as [EBP+8]. Every thread
     * routine that used its parameter therefore got the sentinel 0xDEADBEEF
     * and dereferenced it: the crash was "SIGSEGV at 0xdeadbeef" with EAX
     * holding it, one instruction into a freshly started thread, and it only
     * showed up when the game finally started a thread that USES its
     * argument -- the ones that ignore it had been working all along.
     */
    C.esp = t->stack_base + t->stack_bytes - 16u;
    WR32(C.esp, t->arg);
    t->depth = 1;                        /* it is running guest code */
    x86_guest_call_args(&C, t->start, 4u);
    t->exit_code = C.eax;

    t->finished = 1;
    t->state = TS_DONE;
    t->state_since = now_s();
    g_exited++;
    k32_handle_thread_done(t);
    guest_cond_broadcast();
    /* Never returns: a finished coroutine has nowhere to return TO. The
       scheduler will not pick it again because `finished` is set. */
    for (;;) sched_switch();
}

uint32_t guest_thread_create(uint32_t start, uint32_t arg, uint32_t stack_bytes,
                             uint32_t *tid_out)
{
    return guest_thread_create_ex(start, arg, stack_bytes, 0, tid_out);
}

uint32_t guest_thread_create_ex(uint32_t start, uint32_t arg,
                                uint32_t stack_bytes, int suspended,
                                uint32_t *tid_out)
{
    GuestThread *t = NULL;
    int i;
    char *hs;

    if (!g_self) sched_attach_main();
    for (i = 0; i < MAX_THREADS; i++) if (!g_thread[i].used) { t = &g_thread[i]; break; }
    if (!t) {
        fprintf(stderr, "threads: all %d guest thread slots are live. This is "
                        "a fixed table, not a leak report -- raise MAX_THREADS "
                        "in src/native/threads.c.\n", MAX_THREADS);
        return 0;
    }
    memset(t, 0, sizeof *t);
    t->slot = i;
    t->stack_bytes = stack_bytes ? ((stack_bytes + 0xFFFu) & ~0xFFFu)
                                 : STACK_DEFAULT;
    t->stack_base = guest_malloc(t->stack_bytes);
    t->tib = guest_malloc(TIB_BYTES);
    if (!t->stack_base || !t->tib) {
        fprintf(stderr, "threads: no guest memory for a %u-byte stack and a "
                        "TIB; the thread is NOT created and the caller is told "
                        "so.\n", t->stack_bytes);
        if (t->stack_base) guest_free(t->stack_base);
        if (t->tib) guest_free(t->tib);
        return 0;
    }
    hs = mmap(NULL, HSTACK_BYTES, PROT_READ | PROT_WRITE,
              MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (hs == MAP_FAILED) {
        fprintf(stderr, "threads: no host stack for the new guest thread (%s); "
                        "it is NOT created rather than started on a stack that "
                        "would overflow into something else.\n",
                strerror(errno));
        guest_free(t->stack_base); guest_free(t->tib);
        return 0;
    }
    mprotect(hs, HSTACK_GUARD, PROT_NONE);      /* fault, do not corrupt */
    t->hstack = hs;
    t->used = 1;
    /* Stamped at creation, not at the first state change: a thread that has
       not run yet reported its age as the process uptime -- 8,989 seconds on
       a 130-second run, which is the instrument lying rather than the thread
       being stuck. */
    t->state = TS_NEW;
    t->state_since = now_s();
    t->suspended = suspended;
    t->start = start;
    t->arg = arg;
    t->tid = g_next_tid++;
    t->handle = k32_handle_for_thread(t);
    if (!t->handle) { munmap(hs, HSTACK_BYTES); t->used = 0; return 0; }

    getcontext(&t->ctx);
    t->ctx.uc_stack.ss_sp   = hs + HSTACK_GUARD;
    t->ctx.uc_stack.ss_size = HSTACK_BYTES - HSTACK_GUARD;
    t->ctx.uc_link          = NULL;
    makecontext(&t->ctx, thread_trampoline, 0);
    t->fsbase = t->tib;

    /*
     * It does not run yet -- not because it is held off, but because the
     * creator is the running thread and a switch happens only where this file
     * says. CREATE_SUSPENDED needs no separate mechanism either: the scheduler
     * simply never picks a suspended thread, so "suspended" means "has not
     * entered the guest routine", which is what the caller means by it.
     */
    g_created++;
    if (tid_out) *tid_out = t->tid;
    if (g_created == 1)
        printf("threads: the first GUEST THREAD exists (start 0x%08x, "
               "%u-byte guest stack, 8 MB host stack). Guest threads are "
               "COROUTINES on this one host thread and the schedule is this "
               "program's, not the OS's -- see src/native/threads.c.\n",
               start, t->stack_bytes);
    return t->handle;
}

/*
 * A CLOSED handle stops naming this thread -- and that is a correctness rule,
 * not bookkeeping.
 *
 * kernel32 hands out small table indices and CloseHandle frees the slot, so
 * the NEXT _beginthreadex gets the same number. The GuestThread record kept
 * its handle for ever, so by_handle() -- scanning in creation order -- matched
 * the DEAD thread first. The intro movie ends, the game starts the next one,
 * and every ResumeThread aimed at the new decoder woke a thread that had
 * already exited: the new one stayed CREATE_SUSPENDED for the rest of the run
 * while the main thread waited for a frame it could never produce. It looked
 * exactly like a deadlock in the movie player, and 9,000,634 resumes on a dead
 * thread was the only visible trace.
 *
 * The stacks go back here too -- 256 KB of guest arena, a TIB, and 8 MB of
 * host address space -- and three threads are created per movie, so holding
 * them until exit is a leak with a very short fuse.
 */
void guest_thread_handle_closed(uint32_t handle)
{
    GuestThread *t = (GuestThread *)k32_thread_record(handle);
    if (t && t->used) {
        if (t->handle == handle) t->handle = 0;
        if (t->finished) {
            /* This call happens before kernel32 clears the closing alias, so
               one means it is the LAST open handle to this thread object. */
            if (k32_thread_handle_count(t) == 1) {
                if (t->stack_base) guest_free(t->stack_base);
                if (t->tib) guest_free(t->tib);
                if (t->hstack) munmap(t->hstack, HSTACK_BYTES);
                t->stack_base = t->tib = 0;
                t->hstack = NULL;
                t->reaped = 1;
                t->used = 0;          /* slot is free for the next thread */
                g_reaped++;
            }
        }
    }
}

static GuestThread *by_handle(uint32_t h)
{
    GuestThread *t = (GuestThread *)k32_thread_record(h);
    return t && t->used ? t : NULL;
}

int guest_thread_is_thread(uint32_t handle) { return by_handle(handle) != NULL; }

int guest_thread_finished(uint32_t handle, uint32_t *exit_code)
{
    GuestThread *t = by_handle(handle);
    if (!t) return 0;
    if (exit_code) *exit_code = t->exit_code;
    return t->finished;
}

int guest_thread_join(uint32_t handle, uint32_t ms)
{
    GuestThread *t = by_handle(handle);
    if (!t) return 0;
    while (!t->finished) {
        if (ms == 0) return 0;
        guest_cond_wait_ms(ms);
        if (ms != 0xFFFFFFFFu) break;      /* one timed wait, then re-check */
    }
    return t->finished;
}

/*
 * SuspendThread, and why it is exact here rather than approximate.
 *
 * Win32's guarantee is that once it returns, the target is not executing guest
 * code. That is already true of every thread but the running one, so a suspend
 * of another thread is just a flag the scheduler honours. The case libCriMovie
 * actually needs is a thread suspending ITSELF: its decoder loop (libCriMovie
 * 0x10002630) restores its priority and calls SuspendThread on its own handle
 * once it has run out of work, waiting to be resumed by whoever wants another
 * frame -- a park, not a kill. Self-suspend therefore yields here.
 *
 * Nothing nests: Win32 counts suspends, this host has 0 or 1, and a second
 * suspend of an already-suspended thread is refused by name rather than
 * counted wrong -- a wrong count means a ResumeThread that should have woken a
 * thread silently does not.
 */
static void guest_suspend_point(void)
{
    GuestThread *t = g_self;
    if (!t || !t->suspended) return;
    state_set(TS_SUSPENDED);
    while (t->suspended) sched_switch();
    state_set(TS_RUNNING);
}

int guest_thread_suspend(uint32_t handle)
{
    GuestThread *t = by_handle(handle);
    if (!t) { g_suspend_unknown++; return -1; }
    if (t->suspended) {
        fprintf(stderr, "threads: SuspendThread on a thread that is already "
                        "suspended. Win32 would COUNT that and require as many "
                        "resumes; this host has one flag, so it is refused "
                        "rather than miscounted -- a resume that then failed to "
                        "wake it would be silent.\n");
        return -1;
    }
    t->suspended = 1;
    g_suspends++;
    t->n_suspend++;
    /* Announced once, with WHICH thread and whether it parked itself: the
       difference between "a thread was suspended" and "the thread suspended
       itself and is waiting for someone to resume it" is the difference
       between a control operation and a rendezvous, and a run that stalls
       afterwards is read completely differently depending on which it was. */
    if (g_suspends == 1)
        printf("threads: the first SuspendThread -- tid %u %s.\n",
               t->tid, t == g_self ? "suspended ITSELF and is now waiting to be "
                                     "resumed" : "was suspended by another thread");
    fflush(stdout);
    if (t == g_self) guest_suspend_point();
    return 0;                            /* the previous count */
}

/* ResumeThread: 1 if it was suspended and now is not, 0 if it was already
   running. Win32 returns the PREVIOUS suspend count, and this host only ever
   has 0 or 1 of them -- nothing nests suspends here, and a nested one would be
   refused rather than counted wrong. */
int guest_thread_resume(uint32_t handle)
{
    GuestThread *t = by_handle(handle);
    int was;
    if (!t) { g_resume_unknown++; return -1; }
    was = t->suspended;
    t->suspended = 0;
    g_resumes++;
    t->n_resume++;
    if (!was && ++g_resume_noop == 100000ul)
        fprintf(stderr,
                "threads: ResumeThread has been called %lu times on tid %u "
                "(start 0x%08x), which was NOT suspended on any of them -- so "
                "every one was a no-op.\n"
                "  Something is spinning on it while waiting for something "
                "else. Reported once, at the hundred-thousandth.\n",
                g_resume_noop, t->tid, t->start);
    if (g_resumes == 1) {
        printf("threads: the first ResumeThread -- tid %u, which %s.\n",
               t->tid, was ? "was suspended and will now continue"
                           : "was NOT suspended, so this changed nothing");
        fflush(stdout);
    }
    /*
     * AND NOTHING ELSE -- deliberately. Win32's ResumeThread makes the target
     * runnable and returns; it does not wait, and neither does this. The
     * resumed thread runs when the scheduler next reaches it, which the
     * quantum guarantees happens.
     *
     * Two hand-off designs have been MEASURED here and both failed, which is
     * worth more than either would have been if it worked. libCriMovie's
     * decoder does not park between slices -- it parks only when it runs out
     * of work -- and its partner spins on a flag rather than blocking. The
     * first hand-off yielded on every resume and livelocked; the second waited
     * for the resumed thread to park and froze the run at frame 2639 with 3.17
     * billion boundary crossings a minute. Both sides spin, so this is a
     * scheduling problem, and preemption is what solves it. Issue #57.
     */
    return was;
}

void guest_thread_exit(uint32_t code)
{
    GuestThread *t = g_self;
    if (!t || t->is_main) {
        fprintf(stderr, "threads: _endthreadex on the MAIN thread, which Win32 "
                        "does not allow and this host cannot honour -- the main "
                        "thread is the process.\n");
        return;
    }
    t->exit_code = code;
    t->finished = 1;
    t->state = TS_DONE;
    g_exited++;
    k32_handle_thread_done(t);
    guest_cond_broadcast();
    for (;;) sched_switch();             /* never picked again */
}

/*
 * The ENGINE's own view of its threads, read out of guest memory.
 *
 * Issue #61: shutdown faults in igThreadManager::userUnregister because
 * igPthreadThreadManager::getCallingThread returned NULL, and that function
 * returns NULL for two different reasons -- an empty list (an ordering
 * problem) or a list nothing matches (an identity problem, which this port's
 * one-host-thread coroutine model makes worth asking about). Those are
 * distinguishable by looking, and guessing between them is how issue #54 went
 * wrong, so this looks.
 *
 * Layout, read out of libIGCore rather than assumed: the singleton pointer
 * lives at 0x1015f438 in the module's LINKED image, the thread array object
 * hangs at manager+8, its count is array+8 and its element pointers array+0x10,
 * and each thread's stored id is thread+0x40 (getCallingThread at 0x10064700
 * compares exactly that against pthread_self).
 */
#define IGCORE_THREADMGR_PTR 0x1015f438u

/*
 * A guest pointer this may dereference, or 0.
 *
 * The first version of this report read mgr+8, then arr+0x10, then each
 * element, trusting the layout. On a run stopped mid-movie the manager held
 * something this walk could not follow and the report took a SIGSEGV -- inside
 * the SHUTDOWN report, which is the one place a diagnostic must not fault,
 * because it takes every other report down with it. A layout read out of a
 * disassembly is a hypothesis, and a diagnostic that bets the process on one
 * is not a diagnostic.
 *
 * So every read is bounds-checked first: the address must lie in the guest
 * heap or inside a mapped module. A pointer that does not is REPORTED, not
 * followed -- and that report is itself a finding, because it says the layout
 * is wrong rather than pretending the list is empty.
 */
static void tm_modules_dump(void)
{
    X86Module *m;
    printf("          the ranges checked were the guest heap and these "
           "modules:\n");
    for (m = x86_modules(); m; m = m->next) {
        uint32_t b = m->base ? *m->base : 0u;
        printf("            %-20s base 0x%08x size 0x%08x%s\n",
               m->name ? m->name : "(unnamed)", b, m->size,
               b ? "" : "   <- never mapped, so nothing lands in it");
    }
}

/*
 * The pthread handle each guest coroutine holds, read out of Win32 TLS.
 *
 * The engine's vendored pthread_self is TlsGetValue on the key at 0x1015f4d8
 * (FUN_10075400 -> FUN_10075ff0 -> KERNEL32!TlsGetValue, confirmed by walking
 * libIGCore.dll's import table). So the handle getCallingThread compares
 * against is literally one of these words, and printing them per slot says
 * which coroutine the engine would consider the caller.
 *
 * The key is read from guest memory, so a build where it has not been
 * allocated yet says so rather than printing a column of zeroes that look like
 * an answer.
 */
#define IGCORE_PTHREAD_TLS_KEY 0x1015f4d8u

static int tm_readable(uint32_t a);

static void tm_tls_handles(X86Module *core)
{
    uint32_t keyslot, keyobj, key;
    int slot, shown = 0;

    keyslot = *core->base + (IGCORE_PTHREAD_TLS_KEY - core->preferred);
    if (!tm_readable(keyslot) || !(keyobj = RD32(keyslot))
        || !tm_readable(keyobj)) {
        printf("          pthread TLS key: not allocated yet (slot 0x%08x), so "
               "no coroutine has a pthread handle to compare.\n", keyslot);
        return;
    }
    key = RD32(keyobj);            /* FUN_10075ff0 loads [arg] then TlsGetValue */
    printf("          pthread handles by guest-thread slot (TLS index %u, the "
           "value getCallingThread compares):\n", key);
    for (slot = 0; slot < k32_tls_slot_count(); slot++) {
        uint32_t h = k32_tls_peek(slot, key);
        if (!h) continue;
        printf("            slot %-2d handle 0x%08x\n", slot, h);
        shown++;
    }
    if (!shown)
        printf("            NONE of the %d slots holds a handle -- every "
               "coroutine would allocate a fresh one on its next "
               "pthread_self(), and none of them can match a thread registered "
               "earlier.\n", k32_tls_slot_count());
}

static int tm_readable(uint32_t a)
{
    uint32_t v;
    /*
     * "Is it MAPPED", not "is it in a range I know about".
     *
     * The range version of this refused a perfectly good pointer: the engine's
     * igThreadManager lives at 0x00a8a098, which is past XMen2.exe's
     * SizeOfImage (0x006744c6, confirmed against the PE) and outside the guest
     * heap, because the engine allocates it from its OWN pool. A range check
     * called that unreadable and the report said the layout must be wrong,
     * which was a wrong conclusion drawn from a correct measurement of the
     * wrong thing. process_vm_readv answers the question actually being asked
     * and still cannot fault.
     */
    if (a < 0x1000u) return 0;
    return x86_peek32(a, &v);
}

void guest_engine_thread_report(void)
{
    X86Module *m;
    uint32_t slot, mgr, arr, n, elems, i;

    /* Modules register with their FILE name -- "libIGCore.dll", not
       "libIGCore". Matching the bare stem found nothing and said so, which
       read as "the module is not linked" on a build that links it. Compare
       the way the loader does: case-insensitively, extension and all. */
    for (m = x86_modules(); m; m = m->next)
        if (m->name && !strcasecmp(m->name, "libIGCore.dll")) break;
    if (!m || !m->base) {
        int n = 0;
        X86Module *k;
        for (k = x86_modules(); k; k = k->next) n++;
        printf("  engine threads: no module named libIGCore.dll among the %d "
               "linked, so the engine's thread manager could not be read AT "
               "ALL. The names present are:", n);
        for (k = x86_modules(); k; k = k->next)
            printf(" %s", k->name ? k->name : "(unnamed)");
        printf("\n");
        return;
    }
    /*
     * X86Module::base is a POINTER TO the guest base, not the base.
     *
     * Every other user in this codebase writes `*m->base`; this one wrote
     * `(uintptr_t)m->base` and so read the ADDRESS OF the generated module's
     * `g_imgbase_libIGCore` global. That is a host address, which is why the
     * report announced a module "mapped at 0x563f50c5b1c8, above 4 GB" -- a
     * true statement about the wrong quantity -- and why an earlier run
     * produced a plausible-looking 0x72080600 that was equally meaningless.
     * The two crashes before that came from the same mistake reaching RD32.
     */
    {
        uint32_t imgbase = m->base ? *m->base : 0u;
        if (!imgbase) {
            printf("  engine threads: libIGCore.dll is registered but has no "
                   "image base, so the host never mapped it. NOTHING was "
                   "read.\n");
            return;
        }
        slot = imgbase + (IGCORE_THREADMGR_PTR - m->preferred);
    }
    if (!tm_readable(slot)) {
        printf("  engine threads: the singleton slot computes to 0x%08x, which "
               "is not readable guest memory (libIGCore.dll at 0x%08x, linked "
               "for 0x%08x). NOTHING was read.\n",
               slot, *m->base, m->preferred);
        return;
    }
    mgr = RD32(slot);
    if (!mgr) {
        printf("  engine threads: the igThreadManager singleton is NULL "
               "(slot 0x%08x). Either it was never registered, or "
               "userUnregister already cleared it -- it writes 0 there on its "
               "way out.\n", slot);
        return;
    }
    if (!tm_readable(mgr) || !tm_readable(mgr + 8u)) {
        printf("  engine threads: the singleton at slot 0x%08x holds 0x%08x, "
               "which is in NEITHER the guest heap NOR any mapped module. Not "
               "followed. Either the manager is not what this diagnostic "
               "thinks it is, or the run caught it mid-teardown.\n", slot, mgr);
        tm_modules_dump();
        return;
    }
    arr = RD32(mgr + 8u);
    if (!arr) {
        printf("  engine threads: the manager at 0x%08x holds a NULL thread "
               "array, so getCallingThread cannot match ANYTHING.\n", mgr);
        return;
    }
    if (!tm_readable(arr) || !tm_readable(arr + 0x10u)) {
        printf("  engine threads: manager 0x%08x names a thread array at "
               "0x%08x, which is not readable guest memory. Not followed.\n",
               mgr, arr);
        return;
    }
    n = RD32(arr + 8u);
    elems = RD32(arr + 0x10u);
    /* Printed at zero too, with the addresses that produced it: "the list is
       empty" and "the list was never read" must not look the same. */
    printf("  engine threads: igThreadManager 0x%08x, array 0x%08x, %u "
           "thread(s) registered%s\n", mgr, arr, n,
           n ? ":" : " -- EMPTY, which is one of issue #61's two candidates");
    if (n > 64u) {
        printf("          count %u is not credible; refusing to walk it.\n", n);
        return;
    }
    if (n && !tm_readable(elems)) {
        printf("          the element array at 0x%08x is not readable; the "
               "count above stands but the entries cannot be listed.\n", elems);
        return;
    }
    for (i = 0; i < n && elems; i++) {
        uint32_t t;
        if (!tm_readable(elems + i * 4u)) break;
        t = RD32(elems + i * 4u);
        if (!tm_readable(t) || !tm_readable(t + 0x40u)) {
            printf("          [%u] thread 0x%08x -- not readable, not "
                   "followed\n", i, t);
            continue;
        }
        printf("          [%u] thread 0x%08x  id 0x%08x  refcount %u\n",
               i, t, RD32(t + 0x40u), RD32(t + 4u));
    }
    tm_tls_handles(m);
}

void guest_thread_report(void)
{
    int i, live = 0;
    for (i = 0; i < MAX_THREADS; i++)
        if (g_thread[i].used && !g_thread[i].finished) live++;
    if (!g_created) {
        printf("  threads: no guest thread was ever created; everything ran on "
               "the main thread.\n");
    } else {
        printf("  threads: %lu created, %lu exited, %lu reaped (handle closed, "
               "stacks freed), %d still running; %lu suspend(s), %lu resume(s)\n",
               g_created, g_exited, g_reaped, live, g_suspends, g_resumes);
        if (g_resume_noop)
            printf("         %lu resume(s) were of a thread that was NOT "
                   "suspended -- Win32 no-ops those, and a loop doing them is "
                   "waiting for something else.\n", g_resume_noop);
        if (g_resume_unknown || g_suspend_unknown)
            printf("         %lu resume(s) and %lu suspend(s) named NO live "
                   "thread -- a handle whose thread had already been reaped. A "
                   "loop doing that is waiting for something that cannot "
                   "happen.\n", g_resume_unknown, g_suspend_unknown);
        for (i = 0; i < MAX_THREADS; i++) {
            GuestThread *t = &g_thread[i];
            /* Reaped slots are printed too, until they are reused: their
               counters are the only record of where a spin loop's resumes
               went, and skipping them is what made 9,000,634 of them
               invisible. */
            if (!t->used && !t->tid) continue;
            printf("         tid %u  start 0x%08x  %lu suspend(s) %lu "
                   "resume(s) %lu turn(s)%s%s\n",
                   t->tid, t->start, t->n_suspend, t->n_resume, t->n_ran,
                   !t->used ? "  REAPED" : t->finished ? "  EXITED" : "",
                   t->suspended ? "  SUSPENDED NOW -- if the run stalled, this "
                                  "is a thread waiting for a ResumeThread that "
                                  "never came" : "");
        }
    }
    /* Flushed, because this now runs from the abort paths too and an unflushed
       stdout buffer is discarded by abort() -- the report was written, and
       vanished, on exactly the stall it exists to explain. */
    fflush(stdout);
    /*
     * Printed even when they are ZERO, with their denominators. "0
     * preemptions" and "preemption is not compiled in" are different facts and
     * a line that only appears when the number is non-zero cannot tell them
     * apart -- and zero here is itself the answer to "why did two spinning
     * threads not take turns".
     */
    printf("         %lu coroutine switch(es), %lu of them preemptions at a "
           "quantum of %lu boundary crossing(s)%s\n",
           g_switches, g_quanta, g_quantum,
           g_quanta ? "" : " -- NO preemption happened: either no second guest "
                           "thread was ever runnable, or the quantum is larger "
                           "than this run");
    printf("         the scheduler found nobody to run %lu time(s)%s\n",
           g_idle_spins,
           g_idle_spins ? " (it pumps the multimedia timers and sleeps to the "
                          "earliest deadline on those passes)"
                        : " -- so a guest thread was always ready");
    fflush(stdout);
}
