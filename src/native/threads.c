/*
 * Guest threads, under ONE global lock.
 *
 * libCriMovie asks for a decoding thread before it will play the intro movie
 * (issue #43), and every subsystem after it -- streaming sound, background
 * loading -- will ask for the same thing. So this is the general answer rather
 * than a way around one caller.
 *
 * ONE LOCK, AND WHY. The register file is a plain struct on the C stack, so it
 * is already per-thread and costs nothing. Everything ELSE this host owns is
 * single-threaded by an assumption nobody wrote down: the kernel32 handle
 * table, the guest heap's free lists, the VirtualAlloc reservation table, the
 * D3D8 object and resource tables, the boundary ring, the CRT's own statics.
 * Auditing all of that at once is how a threading change becomes a month of
 * heisenbugs.
 *
 * So exactly one guest thread runs at a time, and the lock is released only at
 * points where the guest is not executing:
 *
 *   Sleep                      - the whole point of it is to let others run
 *   WaitForSingleObject/...    - and it must, or the signaller can never run
 *   joining a thread           - same
 *
 * That is a real threading model: threads exist, they run guest code, they
 * block and are woken, and the things they synchronise on work. What it is NOT
 * is parallel -- two guest threads never execute at once, so a title that
 * expects a decoder to keep up while the main thread spins WITHOUT sleeping or
 * waiting would starve it. If that happens it shows up as a stall, and the fix
 * is to narrow the lock around a named subsystem with evidence, not to widen it
 * everywhere on a guess.
 *
 * PER-THREAD STATE that is not the register file:
 *   - FS base. FS:[0] is the SEH chain head and every recompiled prologue
 *     writes it. A shared g_fsbase would have two threads' exception chains
 *     overwriting each other, so it is __thread and each guest thread gets its
 *     own TIB page.
 *   - The TLS slots. TlsGetValue/TlsSetValue are per-thread BY DEFINITION;
 *     sharing them is not an approximation, it is the opposite of what they
 *     mean.
 *   - The guest stack. Each thread gets its own out of the guest arena.
 */
#include "threads.h"

#include "x86rt.h"
#include "x86rt_native.h"
#include "guest_heap.h"
#include "pe_map.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <sched.h>

/* ---- what each guest thread is doing ------------------------------------ */

enum { TS_NEW = 0, TS_RUNNING, TS_LOCK, TS_COND, TS_BLOCKING, TS_SUSPENDED,
       TS_DONE };
static const char *const TS_NAME[] = {
    "new", "running guest code", "waiting for the guest lock",
    "in a WAIT (condition variable)", "in a blocking host call",
    "SUSPENDED", "finished"
};
static void state_set(int st);

/* ---- the global guest lock --------------------------------------------- */

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_cond = PTHREAD_COND_INITIALIZER;
static unsigned long   g_contended;      /* times a take had to wait */
static int             g_held;

/*
 * How many threads are BLOCKED on the lock right now, and how deeply this
 * thread holds it. Both exist for guest_quantum below: a yield with nobody
 * waiting is pure cost, and a yield from a nested hold would release a lock
 * this thread's caller still believes it has.
 */
static volatile int    g_waiters;
static __thread int    t_depth;
static unsigned long   g_quanta;         /* yields actually performed */
static unsigned long   g_quantum = 20000;

void guest_lock(void)
{
    if (pthread_mutex_trylock(&g_lock) == 0) { t_depth++; state_set(TS_RUNNING); return; }
    g_contended++;
    g_waiters++;
    state_set(TS_LOCK);
    pthread_mutex_lock(&g_lock);
    g_waiters--;
    t_depth++;
    state_set(TS_RUNNING);
}

void guest_unlock(void) { t_depth--; pthread_mutex_unlock(&g_lock); }

/*
 * PREEMPTION BY QUANTUM.
 *
 * The lock was released only at named syscalls -- a wait, a Sleep, a join --
 * which is enough for a guest thread that BLOCKS and useless for one that
 * SPINS. The engine's movie rendezvous has both sides spinning (issue #57):
 * one polls a flag, the other polls the decoder, and neither ever reaches a
 * release point, so whichever took the lock first held it forever. That
 * deadlocked about one run in six, and starved the cutscene to 1.7 frames a
 * second on the runs that survived -- 654 real resumes over 380 seconds.
 *
 * A hand-off at ResumeThread was tried first and made it WORSE (recorded as a
 * measured dead end in issue #57): handing the lock to a thread that also
 * spins just moves the starvation. The fix has to be preemption that does not
 * depend on either side cooperating, which is what a real OS provides and what
 * this is: every `quantum` boundary crossings the running thread drops the
 * lock, lets the scheduler pick, and takes it back.
 *
 * One guest thread still executes at a time -- that invariant is untouched,
 * and everything built on it (the host D3D8 layer, the heap) is as safe as it
 * was. What changes is only WHICH one, and how often that can change.
 */
void guest_quantum(void)
{
    if (t_depth != 1 || g_waiters == 0) return;
    g_quanta++;
    guest_unlock();
    sched_yield();
    guest_lock();
}

void guest_quantum_configure(unsigned long crossings)
{
    g_quantum = crossings;
}

/*
 * X2_QUANTUM: boundary crossings between preemptions. 0 disables it, which is
 * the CONTROL -- issue #57 is intermittent, so "it stopped happening" is only
 * evidence next to a build where it still does.
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


/* Defined with the threads below; declared here because guest_blocking_end is
   where every blocked thread comes back through. */
static void guest_suspend_point(void);

void guest_blocking_begin(void) { state_set(TS_BLOCKING); guest_unlock(); }
void guest_blocking_end(void)
{
    guest_lock();
    /* A thread that was SUSPENDED while it sat in a wait must not carry on
       executing guest code just because the wait ended. This is the one place
       every blocked thread comes back through, which is why the check lives
       here rather than at each caller. */
    guest_suspend_point();
}

/* The condition variable every guest wait uses, so a signal wakes whoever is
   waiting whatever they are waiting FOR. One condvar rather than one per
   object: waits here are rare and a spurious wake costs a re-check, while a
   condvar per handle would have to be created and destroyed with the handle. */
void guest_cond_wait_ms(uint32_t ms)
{
    state_set(TS_COND);
    if (ms == 0xFFFFFFFFu) {
        pthread_cond_wait(&g_cond, &g_lock);
        state_set(TS_RUNNING);
        return;
    }
    {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec  += (time_t)(ms / 1000u);
        ts.tv_nsec += (long)(ms % 1000u) * 1000000L;
        if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
        pthread_cond_timedwait(&g_cond, &g_lock, &ts);
    }
    state_set(TS_RUNNING);
}

void guest_cond_broadcast(void) { pthread_cond_broadcast(&g_cond); }

/* ---- per-thread state --------------------------------------------------- */

/* Declared in x86rt.h and __thread there too: every recompiled body's SEH
   prologue writes FS:[0], so this cannot be shared. */
extern __thread uint32_t g_fsbase;

/* ---- the threads -------------------------------------------------------- */

#define MAX_THREADS   16
#define TIB_BYTES     0x1000u
#define STACK_DEFAULT (256u * 1024u)

typedef struct {
    int       used, finished, suspended;
    pthread_t th;
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
    unsigned long n_suspend, n_resume;
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
} GuestThread;


static GuestThread g_thread[MAX_THREADS];
/* The slot of the thread running on THIS host thread; NULL on the main one,
   which has no slot. Declared here rather than beside thread_main because the
   state helpers below need it. */
static __thread GuestThread *g_self;

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

/*
 * WHICH guest thread is running, in the guest's own numbering.
 *
 * GetCurrentThreadId used to return the HOST thread id, which was harmless
 * only for as long as nothing compared two of them. Critical sections do
 * exactly that -- an owner field is a thread id -- so the guest number is the
 * one that has to come out here. The main thread has no slot in the table and
 * gets the id below g_next_tid's base, so every thread including it has one
 * and no two share.
 */
#define MAIN_TID 999u
uint32_t guest_current_tid(void)
{
    return g_self ? g_self->tid : MAIN_TID;
}

static void state_set(int st)
{
    if (!g_self) return;
    if (g_self->state == st) return;
    g_self->state = st;
    g_self->state_since = now_s();
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
    for (i = 0; i < MAX_THREADS; i++) {
        GuestThread *g = &g_thread[i];
        if (!g->used || g->finished) continue;
        live++;
        fprintf(stderr, "[HB]           tid %u start 0x%08x: %s for %.1fs\n",
                g->tid, g->start,
                TS_NAME[g->state < 0 || g->state > TS_DONE ? 0 : g->state],
                t - g->state_since);
    }
    if (!live)
        fprintf(stderr, "[HB]           no live guest thread other than the "
                        "main one, which is not in this table\n");
}
static unsigned long g_created, g_exited, g_suspends, g_resumes, g_reaped;
/*
 * Resumes and suspends that named NO live thread.
 *
 * The count that mattered in issue #50 was invisible: a ResumeThread whose
 * handle matches a thread that has since been reaped simply returns -1, and a
 * spin loop doing that eighteen million times looks exactly like a spin loop
 * doing nothing. Counted separately from the resumes that landed, because
 * "resumed a corpse" and "resumed a running thread" are different bugs.
 */
static unsigned long g_resume_unknown, g_suspend_unknown;
/*
 * Resumes of a thread that was NOT suspended.
 *
 * Win32 defines that as a no-op returning 0, so it cannot fail -- and a loop
 * doing it is a loop waiting for something else entirely, which is what a
 * stall looks like from in here. Reported once at a threshold, WITH the thread
 * it names, because the per-slot counters are lost the moment a reaped slot is
 * reused and by then the spin is invisible again.
 */
static unsigned long g_resume_noop;
static uint32_t g_next_tid = 1000;

/* The kernel32 handle table owns handles; this is the one hook into it. */
uint32_t k32_handle_for_thread(void *rec);
void     k32_handle_thread_done(uint32_t handle);

static void *thread_main(void *p)
{
    GuestThread *t = (GuestThread *)p;
    CPU C;

    guest_lock();
    g_self = t;
    /* CREATE_SUSPENDED: the thread exists and is not running guest code. It
       waits here rather than at the pthread level, because "suspended" has to
       mean "has not started the guest routine" -- a thread that had already
       run one instruction would not be resumable in the sense the caller
       means. */
    while (t->suspended) { state_set(TS_SUSPENDED);
                           pthread_cond_wait(&g_cond, &g_lock); }
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
    x86_guest_call(&C, t->start);
    t->exit_code = C.eax;

    t->finished = 1;
    g_exited++;
    k32_handle_thread_done(t->handle);
    guest_cond_broadcast();
    guest_unlock();
    return NULL;
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
    int i, rc;

    for (i = 0; i < MAX_THREADS; i++) if (!g_thread[i].used) { t = &g_thread[i]; break; }
    if (!t) {
        fprintf(stderr, "threads: all %d guest thread slots are live. This is "
                        "a fixed table, not a leak report -- raise MAX_THREADS "
                        "in src/native/threads.c.\n", MAX_THREADS);
        return 0;
    }
    memset(t, 0, sizeof *t);
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
    if (!t->handle) { t->used = 0; return 0; }

    /*
     * Created while HOLDING the guest lock, and the new thread's first act is
     * to take it -- so it cannot start running guest code until the creator
     * next releases it. Without that, a thread could be running before the
     * caller has even stored the handle it is about to be asked about.
     */
    rc = pthread_create(&t->th, NULL, thread_main, t);
    if (rc != 0) {
        fprintf(stderr, "threads: pthread_create failed (%s); the guest is "
                        "told the thread could not be created.\n", strerror(rc));
        t->used = 0;
        return 0;
    }
    pthread_detach(t->th);
    g_created++;
    if (tid_out) *tid_out = t->tid;
    if (g_created == 1)
        printf("threads: the first GUEST THREAD is running (start 0x%08x, "
               "%u-byte stack). One guest thread executes at a time, under a "
               "global lock released at Sleep and at every wait -- see "
               "src/native/threads.c.\n", start, t->stack_bytes);
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
 * The stack and TIB go back to the guest heap here too. They are 1 MB each and
 * three threads are created per movie, so holding them until exit is a leak
 * with a very short fuse.
 */
void guest_thread_handle_closed(uint32_t handle)
{
    int i;
    for (i = 0; i < MAX_THREADS; i++) {
        GuestThread *t = &g_thread[i];
        if (!t->used || t->handle != handle) continue;
        t->handle = 0;
        if (t->finished) {
            if (t->stack_base) guest_free(t->stack_base);
            if (t->tib) guest_free(t->tib);
            t->stack_base = t->tib = 0;
            t->reaped = 1;
            t->used = 0;              /* the slot is free for the next thread */
            g_reaped++;
        }
        return;
    }
}

static GuestThread *by_handle(uint32_t h)
{
    int i;
    for (i = 0; i < MAX_THREADS; i++)
        if (g_thread[i].used && g_thread[i].handle == h) return &g_thread[i];
    return NULL;
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
 * SuspendThread, and why it can be exact here rather than approximate.
 *
 * Win32's guarantee is that once it returns, the target is not executing guest
 * code. Under the global lock that is already true of every thread but the one
 * running: a suspend is therefore just a flag, provided a parked thread CHECKS
 * it before it starts executing again -- which is what guest_suspend_point()
 * below is, called wherever a thread re-takes the lock.
 *
 * The case libCriMovie actually needs is a thread suspending ITSELF. Its
 * decoder loop (libCriMovie 0x10002630) sets its own priority and then calls
 * SuspendThread on its own handle, waiting to be resumed by whoever wants
 * another frame -- a park, not a kill. Self-suspend therefore has to BLOCK
 * here, releasing the lock so that the thread which will resume it can run.
 *
 * Nothing nests: Win32 counts suspends, this host has 0 or 1, and a second
 * suspend of an already-suspended thread is refused by name rather than
 * counted wrong -- a wrong count means a ResumeThread that should have woken a
 * thread silently does not.
 */
static void guest_suspend_point(void)
{
    GuestThread *t = g_self;
    if (!t) return;                      /* the main thread; see below */
    if (!t->suspended) return;
    /* Set AFTER the wait begins would be a lie for the first interval, and
       guest_cond_wait_ms sets TS_COND -- so the suspended state is stamped
       here and re-stamped on the way out. */
    state_set(TS_SUSPENDED);
    while (t->suspended) {
        pthread_cond_wait(&g_cond, &g_lock);
    }
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
       between a control operation and a rendezvous, and a run that deadlocks
       afterwards is read completely differently depending on which it was. */
    if (g_suspends == 1)
        printf("threads: the first SuspendThread -- tid %u %s. Under this "
               "host's global lock a suspend is exact rather than approximate: "
               "every thread but the running one is already parked, and a "
               "parked thread re-checks the flag before it executes again.\n",
               t->tid, t == g_self ? "suspended ITSELF and is now waiting to be "
                                     "resumed" : "was suspended by another thread");
    fflush(stdout);
    if (t == g_self) {
        /* Blocks, releasing the lock: nothing could resume us otherwise. */
        guest_cond_broadcast();
        guest_suspend_point();
    }
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
    guest_cond_broadcast();
    /*
     * AND NOTHING ELSE -- deliberately. Win32's ResumeThread makes the target
     * runnable and returns; it does not wait, and neither does this.
     *
     * Two hand-off designs have now been MEASURED here and both failed, which
     * is worth more than either would have been if it worked. Read out of the
     * guest, libCriMovie's rendezvous is:
     *
     *   decoder  (0x10002630)  do a slice of work; park with SuspendThread on
     *                          SELF *only when it has run out of work*; loop.
     *   partner  (0x10002520)  set a flag, then SPIN up to 3,000,000 times
     *                          calling SetThreadPriority + ResumeThread until
     *                          the decoder clears it.
     *
     * The first attempt handed the lock over on every resume and slept a
     * millisecond; that livelocked. The second (this one, reverted) waited for
     * the resumed thread to PARK before returning -- and froze the run at
     * frame 2639 with 3.17 BILLION boundary crossings in sixty seconds,
     * because the decoder does not park while it still has work: it stays in
     * its own loop, and the thread waiting for it to park waits for ever.
     *
     * So BOTH sides spin without ever blocking, and no hand-off at a syscall
     * can schedule two spinners -- that is a scheduling problem, not a
     * synchronisation one. What makes this rendezvous complete is preemption
     * that neither side has to cooperate with: guest_quantum() above.
     */
    return was;
}

void guest_thread_exit(uint32_t code)
{
    GuestThread *t = g_self;
    if (!t) {
        fprintf(stderr, "threads: _endthreadex on the MAIN thread, which Win32 "
                        "does not allow and this host cannot honour -- the main "
                        "thread is the process.\n");
        return;
    }
    t->exit_code = code;
    t->finished = 1;
    g_exited++;
    k32_handle_thread_done(t->handle);
    guest_cond_broadcast();
    guest_unlock();
    pthread_exit(NULL);
}

void guest_thread_report(void)
{
    int i, live = 0;
    for (i = 0; i < MAX_THREADS; i++)
        if (g_thread[i].used && !g_thread[i].finished) live++;
    if (!g_created) {
        printf("  threads: no guest thread was ever created; everything ran on "
               "the main thread.\n");
        return;
    }
    printf("  threads: %lu created, %lu exited, %lu reaped (handle closed, "
           "stack freed), %d still running; %lu suspend(s), %lu resume(s)\n",
           g_created, g_exited, g_reaped, live, g_suspends, g_resumes);
    if (g_resume_noop)
        printf("         %lu resume(s) were of a thread that was NOT suspended "
               "-- Win32 no-ops those, and a loop doing them is waiting for "
               "something else.\n", g_resume_noop);
    if (g_resume_unknown || g_suspend_unknown)
        printf("         %lu resume(s) and %lu suspend(s) named NO live thread "
               "-- a handle whose thread had already been reaped. A loop doing "
               "that is waiting for something that cannot happen.\n",
               g_resume_unknown, g_suspend_unknown);
    for (i = 0; i < MAX_THREADS; i++) {
        GuestThread *t = &g_thread[i];
        /* Reaped slots are printed too, until they are reused: their counters
           are the only record of where a spin loop's resumes went, and
           skipping them is what made 9,000,634 of them invisible. */
        if (!t->used && !t->tid) continue;
        printf("         tid %u  start 0x%08x  %lu suspend(s) %lu resume(s)%s%s\n",
               t->tid, t->start, t->n_suspend, t->n_resume,
               !t->used ? "  REAPED" : t->finished ? "  EXITED" : "",
               t->suspended ? "  SUSPENDED NOW -- if the run stalled, this is a "
                              "thread waiting for a ResumeThread that never came"
                            : "");
    }
    /* Flushed, because this now runs from x86_diag_dump on the ABORT paths too
       and an unflushed stdout buffer is discarded by abort() -- the report was
       written, and vanished, on exactly the stall it exists to explain. */
    fflush(stdout);
    if (!g_contended)
        printf("         the guest lock was NEVER contended -- so the threads "
               "created here have not actually overlapped, and nothing in this "
               "run exercised the locking.\n");
    else
        printf("         the guest lock was contended %lu time(s)\n",
               g_contended);
    /*
     * Printed even when it is ZERO, with its denominator. "0 preemptions"
     * and "preemption is not compiled in" are different facts and a line that
     * only appears when the number is non-zero cannot tell them apart -- and
     * zero here is itself the answer to "why did two spinning threads not take
     * turns".
     */
    printf("         %lu preemption(s) at a quantum of %lu boundary "
           "crossing(s)%s\n", g_quanta, g_quantum,
           g_quanta ? "" : " -- NONE happened: either no second guest thread "
                           "ever waited for the lock, or the quantum is "
                           "larger than this run");

    fflush(stdout);
    (void)g_held;
}
