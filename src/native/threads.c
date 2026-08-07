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
#include <string.h>
#include <errno.h>
#include <time.h>

/* ---- the global guest lock --------------------------------------------- */

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_cond = PTHREAD_COND_INITIALIZER;
static unsigned long   g_contended;      /* times a take had to wait */
static int             g_held;

void guest_lock(void)
{
    if (pthread_mutex_trylock(&g_lock) == 0) return;
    g_contended++;
    pthread_mutex_lock(&g_lock);
}

void guest_unlock(void) { pthread_mutex_unlock(&g_lock); }

void guest_blocking_begin(void) { guest_unlock(); }
void guest_blocking_end(void)   { guest_lock(); }

/* The condition variable every guest wait uses, so a signal wakes whoever is
   waiting whatever they are waiting FOR. One condvar rather than one per
   object: waits here are rare and a spurious wake costs a re-check, while a
   condvar per handle would have to be created and destroyed with the handle. */
void guest_cond_wait_ms(uint32_t ms)
{
    if (ms == 0xFFFFFFFFu) {
        pthread_cond_wait(&g_cond, &g_lock);
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
} GuestThread;

static GuestThread g_thread[MAX_THREADS];
static unsigned long g_created, g_exited;
static uint32_t g_next_tid = 1000;

/* The kernel32 handle table owns handles; this is the one hook into it. */
uint32_t k32_handle_for_thread(void *rec);
void     k32_handle_thread_done(uint32_t handle);

static __thread GuestThread *g_self;

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
    while (t->suspended) guest_cond_wait_ms(1000u);
    /* Its own TIB, so this thread's SEH chain is its own. The sentinel is
       Win32's end-of-chain marker; a zero would look like a record at 0 to
       anything that walked it. */
    g_fsbase = t->tib;
    *(volatile uint32_t *)(uintptr_t)t->tib = 0xFFFFFFFFu;

    memset(&C, 0, sizeof C);
    /* The argument, then the return address the thread routine returns to --
       which is never executed, because the return is caught below. */
    C.esp = t->stack_base + t->stack_bytes - 16u;
    WR32(C.esp + 4u, t->arg);
    WR32(C.esp, 0xDEADBEEFu);
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

/* ResumeThread: 1 if it was suspended and now is not, 0 if it was already
   running. Win32 returns the PREVIOUS suspend count, and this host only ever
   has 0 or 1 of them -- nothing nests suspends here, and a nested one would be
   refused rather than counted wrong. */
int guest_thread_resume(uint32_t handle)
{
    GuestThread *t = by_handle(handle);
    int was;
    if (!t) return -1;
    was = t->suspended;
    t->suspended = 0;
    guest_cond_broadcast();
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
    printf("  threads: %lu created, %lu exited, %d still running\n",
           g_created, g_exited, live);
    if (!g_contended)
        printf("         the guest lock was NEVER contended -- so the threads "
               "created here have not actually overlapped, and nothing in this "
               "run exercised the locking.\n");
    else
        printf("         the guest lock was contended %lu time(s)\n",
               g_contended);
    (void)g_held;
}
