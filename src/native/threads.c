#include "x2_log.h"
/*
 * Guest threads, serialized by one process-wide mutex.
 *
 * libCriMovie asks for a decoding thread before it will play the intro movie
 * (issue #43), and every subsystem after it -- streaming sound, background
 * loading -- asks for the same thing. So this is the general answer rather
 * than a way around one caller.
 *
 * Exactly one guest thread executes at a time
 * here: the kernel32 handle table, the guest heap's free lists, the D3D8
 * object tables, the boundary ring and the CRT's statics are all
 * single-threaded by an assumption nobody wrote down, and auditing that at
 * once is how a threading change becomes a month of heisenbugs. Each guest
 * thread therefore has a pthread, but it may enter translated code only while
 * holding the global guest mutex. Condition variables release that mutex at
 * waits and suspend points, and the boundary quantum yields it for spinning
 * guest code.
 *
 * PREEMPTION IS STILL REQUIRED, and this is why. libCriMovie's rendezvous, read
 * out of the guest (issue #57 has the addresses): the decoder loop works until
 * it runs dry and only then parks itself with SuspendThread; its partner sets a
 * flag and SPINS up to 3,000,000 times calling ResumeThread until the decoder
 * clears it. BOTH sides spin and neither blocks. Two hand-off designs were
 * measured and both made it worse. What schedules two spinners is preemption
 * neither side has to cooperate with: x86port executes bounded JIT slices and
 * guest_quantum() yields the lock between them. A host-boundary-only yield is
 * insufficient because this decoder spins on direct guest-to-guest calls.
 *
 * PER-THREAD STATE that is not the register file. The register file is a plain
 * struct on the C stack, so it comes free with the pthread's own stack. The
 * rest is naturally thread-local:
 *   - FS/GS base. FS:[0] is the SEH chain head and every guest prologue
 *     writes it; sharing it would have two threads' exception chains
 *     overwriting each other. Each thread gets its own TIB page.
 *   - The TLS slots. TlsGetValue/TlsSetValue are per-thread BY DEFINITION;
 *     kernel32 keeps one array per slot and this file selects it on lock entry.
 *   - The guest stack, out of the guest arena, and the HOST stack the
 *     guest execution runs on.
 */
#include "guest_clock.h"
#include "guest_heap.h"
#include "guest_memory.h"
#include "pe_map.h"
#include "threads.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <lucent/cvar_c.h>

/* kernel32 owns the handle table and the TLS arrays; these are the hooks. */
uint32_t k32_handle_for_thread(void *rec);
void *k32_thread_record(uint32_t handle);
unsigned k32_thread_handle_count(void *rec);
void k32_handle_thread_done(void *rec);
void k32_tls_switch(int slot);
uint32_t k32_tls_peek(int slot, uint32_t index);
int k32_tls_slot_count(void);
/* ---- what each guest thread is doing ------------------------------------ */

enum {
  TS_NEW = 0,
  TS_RUNNING,
  TS_LOCK,
  TS_COND,
  TS_BLOCKING,
  TS_SUSPENDED,
  TS_DONE
};
static const char *const TS_NAME[] = {"new",
                                      "running guest code",
                                      "runnable, waiting its turn",
                                      "in a WAIT (condition variable)",
                                      "in a blocking host call",
                                      "SUSPENDED",
                                      "finished"};

#define MAX_THREADS 16
#define MAIN_SLOT MAX_THREADS /* the main thread's TLS slot */
/* kernel32.c has to know it too, and before this file gets to run. */
#if MAIN_SLOT != GUEST_MAIN_TLS_SLOT
#error                                                                         \
    "MAIN_SLOT and GUEST_MAIN_TLS_SLOT disagree; kernel32 would give the main thread the wrong TLS"
#endif
#define TIB_BYTES 0x1000u
#define STACK_DEFAULT (256u * 1024u)

#define HSTACK_BYTES (8u * 1024u * 1024u)

typedef struct {
  int used, finished, suspended;
  int slot; /* index in g_thread; also the TLS slot */
  int is_main;
  uint32_t handle; /* the kernel32 handle the guest holds */
  uint32_t tid;
  uint32_t start, arg;
  uint32_t stack_base, stack_bytes;
  uint32_t tib;
  uint32_t exit_code;
  /* Per-thread, because the totals were misleading in exactly the way that
     matters: a run with 3,000,045 resumes and 43 suspends reads as a wildly
     active suspend/resume protocol, and is in fact one thread being resumed
     in a spin while ANOTHER sits parked and is never named. */
  unsigned long n_suspend, n_resume, n_ran;
  int reaped; /* its handle was closed and its memory freed */
  /*
   * WHAT THIS THREAD IS DOING RIGHT NOW, and since when.
   *
   * Three mechanisms were proposed for issue #57's intermittent stall and
   * all three were guesses -- a hand-off, a quantum, a lost pulse -- because
   * nothing here could answer "what is the other thread blocked ON?". The
   * totals could not: a thread parked in a condition wait and a thread
   * spinning in guest code both show up as "1 still running".
   */
  int state;
  double state_since;

  pthread_t thread;
  int depth;        /* guest_lock nesting, for guest_quantum */
  int32_t priority; /* SetThreadPriority, per thread */
} GuestThread;
static GuestThread g_thread[MAX_THREADS + 1]; /* +1: the main thread */
static __thread GuestThread *g_self;
/* Declared in x86rt.h and naturally separated by the host pthreads. */
extern __thread uint32_t g_fsbase, g_gsbase;

/* The guest's clock, not a private one: see guest_clock.h. Five copies of
   this read CLOCK_MONOTONIC directly, and the guest gates real logic on
   elapsed time, so any two of them disagreeing is a timing bug wearing a
   gameplay bug's clothes. */
static double now_s(void) { return guest_clock_now_s(); }

static void state_set(int st) {
  if (!g_self)
    return;
  if (g_self->state == st)
    return;
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
uint32_t guest_current_tid(void) { return g_self ? g_self->tid : MAIN_TID; }

void *guest_thread_current_record(void) {
  if (!g_self)
    sched_attach_main();
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
void guest_thread_priority_set(int32_t p) {
  if (!g_self)
    sched_attach_main();
  g_self->priority = p;
}

int32_t guest_thread_priority_get(void) {
  return g_self ? g_self->priority : 0;
}

/* ---- the scheduler ------------------------------------------------------ */

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cond = PTHREAD_COND_INITIALIZER;
static _Atomic int g_waiters, g_cond_waiters;
static unsigned long g_contended;
static unsigned long g_switches; /* mutex hand-offs performed */
static unsigned long g_quanta;   /* preemptions actually taken */
static unsigned long g_quantum = 20000;
static unsigned long g_created, g_exited, g_suspends, g_resumes, g_reaped;
static unsigned long g_resume_unknown, g_suspend_unknown, g_resume_noop;
static uint32_t g_next_tid = 1000;

static void guest_suspend_point(void);
static int scheduler_has_waiter(void) {
  int i;
  if (g_waiters)
    return 1;
  for (i = 0; i <= MAX_THREADS; i++) {
    GuestThread *t = &g_thread[i];
    if (!t->used || t->finished || t == g_self)
      continue;
    if (t->state == TS_COND ||
        (t->state == TS_SUSPENDED && t->suspended == 0) ||
        (t->state == TS_NEW && t->suspended == 0))
      return 1;
  }
  return 0;
}

/* Attach the process main thread to the same bookkeeping as created threads. */
static void sched_attach_main(void) {
  GuestThread *t = &g_thread[MAIN_SLOT];
  if (t->used)
    return;
  memset(t, 0, sizeof *t);
  t->used = 1;
  t->is_main = 1;
  t->slot = MAIN_SLOT;
  t->tid = MAIN_TID;
  t->state = TS_RUNNING;
  t->state_since = now_s();
  g_self = t;
  k32_tls_switch(MAIN_SLOT);
}

void guest_lock(void) {
  if (!g_self)
    sched_attach_main();
  if (g_self->depth++ > 0)
    return;
  if (pthread_mutex_trylock(&g_lock) != 0) {
    g_contended++;
    g_waiters++;
    state_set(TS_LOCK);
    pthread_mutex_lock(&g_lock);
    g_waiters--;
  }
  k32_tls_switch(g_self->slot);
  guest_suspend_point();
  g_self->n_ran++;
  state_set(TS_RUNNING);
}

void guest_unlock(void) {
  if (!g_self || g_self->depth <= 0)
    return;
  if (--g_self->depth == 0)
    pthread_mutex_unlock(&g_lock);
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
void guest_quantum(void) {
  if (!g_self || g_self->depth != 1 || !scheduler_has_waiter())
    return;
  g_quanta++;
  g_switches++;
  guest_unlock();
  sched_yield();
  guest_lock();
}

void guest_quantum_configure(unsigned long crossings) { g_quantum = crossings; }

/*
 * X2_QUANTUM: boundary crossings between preemptions. 0 disables it, which is
 * the CONTROL -- a scheduling change has to be measured against a build where
 * the mechanism is off, or "it stopped happening" is not evidence.
 */
void guest_quantum_from_env(void) {
  const unsigned long v = (unsigned long)lucent_cvar_number("quantum", 20000);
  if (!v) {
    g_quantum = 0u - 1ul; /* effectively never */
    x2_log_info("threads: X2_QUANTUM=0 -- preemption DISABLED. Two guest "
                "threads that both spin cannot take turns; this is the control "
                "for issue #57, not a configuration to run in.\n");
    return;
  }
  g_quantum = v;
  x2_log_info("threads: preemption quantum set to %lu boundary crossing(s).\n",
              g_quantum);
}

unsigned long guest_quantum_size(void) { return g_quantum; }
unsigned long guest_quantum_count(void) { return g_quanta; }

/*
 * A blocking HOST call -- one this host makes on the guest's behalf that is
 * not a guest wait.
 *
 * Release the serialized guest while the host call blocks so another guest
 * pthread can make progress.
 */
void guest_blocking_begin(void) {
  state_set(TS_BLOCKING);
  guest_unlock();
}
void guest_blocking_end(void) {
  guest_lock();
  /* A thread that was SUSPENDED while it sat in a wait must not carry on
     executing guest code just because the wait ended. This is the one place
     every blocked thread comes back through, which is why the check lives
     here rather than at each caller. */
  guest_suspend_point();
}

/*
 * The wait every guest wait goes through: atomically release the guest mutex
 * and park this pthread until something broadcasts or the deadline passes.
 *
 * One wait queue rather than one per object: waits here are rare and a
 * spurious wake costs a re-check, while a queue per handle would have to be
 * created and destroyed with the handle.
 */
void guest_cond_wait_ms(uint32_t ms) {
  GuestThread *t = g_self;
  if (!t) {
    sched_attach_main();
    t = g_self;
  }
  state_set(TS_COND);
  g_switches++;
  g_cond_waiters++;
  if (ms == 0xFFFFFFFFu) {
    pthread_cond_wait(&g_cond, &g_lock);
  } else {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += (time_t)(ms / 1000u);
    ts.tv_nsec += (long)(ms % 1000u) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) {
      ts.tv_sec++;
      ts.tv_nsec -= 1000000000L;
    }
    pthread_cond_timedwait(&g_cond, &g_lock, &ts);
  }
  g_cond_waiters--;
  k32_tls_switch(t->slot);
  state_set(TS_RUNNING);
  guest_suspend_point();
}

/* Wake everything waiting. Called whenever a guest-visible object is
   signalled -- a thread exiting, an event set, a critical section left. Does
   NOT switch: the caller is in the middle of guest code and Win32's SetEvent
   does not yield either. */
void guest_cond_broadcast(void) {
  if (g_cond_waiters > 0)
    pthread_cond_broadcast(&g_cond);
}

/*
 * Sleep.
 *
 * Sleep(0) is Win32's "give up the rest of my turn" and becomes exactly that.
 * Anything longer parks with a deadline, so the other guest threads run for
 * its duration instead of the whole process stopping -- which is what a
 * usleep() here used to do.
 */
void guest_sleep_ms(uint32_t ms) {
  if (!g_self)
    sched_attach_main();
  if (ms == 0) {
    if (g_self->depth != 1 || !scheduler_has_waiter())
      return;
    g_switches++;
    guest_unlock();
    sched_yield();
    guest_lock();
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
void guest_thread_state_report(void) {
  /* A duration that keeps reading ~0.0s is not a thread that just changed
     state -- it is a thread that keeps WAKING, which is the difference
     between a poll loop and a park and is the thing worth seeing. */
  double t = now_s();
  int i, live = 0;
  for (i = 0; i <= MAX_THREADS; i++) {
    GuestThread *g = &g_thread[i];
    if (!g->used || g->finished)
      continue;
    live++;
    x2_log_error("[HB]           %s%u start 0x%08x: %s for %.1fs%s\n",
                 g->is_main ? "MAIN tid " : "tid ", g->tid, g->start,
                 TS_NAME[g->state < 0 || g->state > TS_DONE ? 0 : g->state],
                 t - g->state_since, g == g_self ? "  <- running" : "");
  }
  if (!live)
    x2_log_error("[HB]           no live guest thread at all, not even "
                 "the main one -- which cannot happen while this line "
                 "is being printed, so the table is wrong\n");
}

/* ---- creating and ending threads ---------------------------------------- */

static void *thread_main(void *argument) {
  GuestThread *t = (GuestThread *)argument;
  CPU C;

  g_self = t;
  guest_lock();
  if (t->suspended)
    state_set(TS_SUSPENDED);
  while (t->suspended) {
    g_cond_waiters++;
    pthread_cond_wait(&g_cond, &g_lock);
    g_cond_waiters--;
  }
  state_set(TS_RUNNING);
  /* Its own TIB, so this thread's SEH chain is its own. The sentinel is
     Win32's end-of-chain marker; a zero would look like a record at 0 to
     anything that walked it. */
  g_fsbase = t->tib;
  *(volatile uint32_t *)guest_memory_pointer(t->tib) = 0xFFFFFFFFu;

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
  C.reg[kX86pEsp] = t->stack_base + t->stack_bytes - 16u;
  WR32(C.reg[kX86pEsp], t->arg);
  x86_guest_call_args(&C, t->start, 4u);
  t->exit_code = C.reg[kX86pEax];

  t->finished = 1;
  t->state = TS_DONE;
  t->state_since = now_s();
  g_exited++;
  k32_handle_thread_done(t);
  guest_cond_broadcast();
  guest_unlock();
  return NULL;
}

uint32_t guest_thread_create(uint32_t start, uint32_t arg, uint32_t stack_bytes,
                             uint32_t *tid_out) {
  return guest_thread_create_ex(start, arg, stack_bytes, 0, tid_out);
}

uint32_t guest_thread_create_ex(uint32_t start, uint32_t arg,
                                uint32_t stack_bytes, int suspended,
                                uint32_t *tid_out) {
  GuestThread *t = NULL;
  pthread_attr_t attr;
  int i, result;

  if (!g_self)
    sched_attach_main();
  for (i = 0; i < MAX_THREADS; i++)
    if (!g_thread[i].used) {
      t = &g_thread[i];
      break;
    }
  if (!t) {
    x2_log_error("threads: all %d guest thread slots are live. This is "
                 "a fixed table, not a leak report -- raise MAX_THREADS "
                 "in src/native/threads.c.\n",
                 MAX_THREADS);
    return 0;
  }
  memset(t, 0, sizeof *t);
  t->slot = i;
  t->stack_bytes =
      stack_bytes ? ((stack_bytes + 0xFFFu) & ~0xFFFu) : STACK_DEFAULT;
  t->stack_base = guest_malloc(t->stack_bytes);
  t->tib = guest_malloc(TIB_BYTES);
  if (!t->stack_base || !t->tib) {
    x2_log_error("threads: no guest memory for a %u-byte stack and a "
                 "TIB; the thread is NOT created and the caller is told "
                 "so.\n",
                 t->stack_bytes);
    if (t->stack_base)
      guest_free(t->stack_base);
    if (t->tib)
      guest_free(t->tib);
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
  if (!t->handle) {
    t->used = 0;
    return 0;
  }

  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, HSTACK_BYTES);
  result = pthread_create(&t->thread, &attr, thread_main, t);
  pthread_attr_destroy(&attr);
  if (result != 0) {
    x2_log_error("threads: pthread_create failed (%s); the guest is "
                 "told the thread could not be created.\n",
                 strerror(result));
    t->used = 0;
    guest_free(t->stack_base);
    guest_free(t->tib);
    return 0;
  }
  pthread_detach(t->thread);

  /*
   * The creator still holds the guest mutex, so the pthread cannot enter
   * translated code before CreateThread has returned its handle.
   */
  g_created++;
  if (tid_out)
    *tid_out = t->tid;
  if (g_created == 1)
    x2_log_info("threads: the first GUEST THREAD exists (start 0x%08x, "
                "%u-byte guest stack, 8 MB host stack). Guest pthreads are "
                "serialized by one mutex and wait on condition variables -- "
                "see src/native/threads.c.\n",
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
void guest_thread_handle_closed(uint32_t handle) {
  GuestThread *t = (GuestThread *)k32_thread_record(handle);
  if (t && t->used) {
    if (t->handle == handle)
      t->handle = 0;
    if (t->finished) {
      /* This call happens before kernel32 clears the closing alias, so
         one means it is the LAST open handle to this thread object. */
      if (k32_thread_handle_count(t) == 1) {
        if (t->stack_base)
          guest_free(t->stack_base);
        if (t->tib)
          guest_free(t->tib);
        t->stack_base = t->tib = 0;
        t->reaped = 1;
        t->used = 0; /* slot is free for the next thread */
        g_reaped++;
      }
    }
  }
}

static GuestThread *by_handle(uint32_t h) {
  GuestThread *t = (GuestThread *)k32_thread_record(h);
  return t && t->used ? t : NULL;
}

int guest_thread_is_thread(uint32_t handle) {
  return by_handle(handle) != NULL;
}

int guest_thread_finished(uint32_t handle, uint32_t *exit_code) {
  GuestThread *t = by_handle(handle);
  if (!t)
    return 0;
  if (exit_code)
    *exit_code = t->exit_code;
  return t->finished;
}

int guest_thread_join(uint32_t handle, uint32_t ms) {
  GuestThread *t = by_handle(handle);
  if (!t)
    return 0;
  while (!t->finished) {
    if (ms == 0)
      return 0;
    guest_cond_wait_ms(ms);
    if (ms != 0xFFFFFFFFu)
      break; /* one timed wait, then re-check */
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
 * Suspend counts nest exactly as Win32 specifies. The old one-bit model
 * returned failure for a second suspend; libCriMovie reacts to that failure by
 * retrying in a tight loop, which turned a parked decoder into hundreds of
 * thousands of calls and diagnostics per second.
 */
static void guest_suspend_point(void) {
  GuestThread *t = g_self;
  if (!t || !t->suspended)
    return;
  state_set(TS_SUSPENDED);
  while (t->suspended) {
    g_cond_waiters++;
    pthread_cond_wait(&g_cond, &g_lock);
    g_cond_waiters--;
  }
  k32_tls_switch(t->slot);
  state_set(TS_RUNNING);
}

int guest_thread_suspend(uint32_t handle) {
  GuestThread *t = by_handle(handle);
  int previous;
  if (!t) {
    g_suspend_unknown++;
    return -1;
  }
  previous = t->suspended;
  t->suspended++;
  g_suspends++;
  t->n_suspend++;
  /* Announced once, with WHICH thread and whether it parked itself: the
     difference between "a thread was suspended" and "the thread suspended
     itself and is waiting for someone to resume it" is the difference
     between a control operation and a rendezvous, and a run that stalls
     afterwards is read completely differently depending on which it was. */
  if (g_suspends == 1)
    x2_log_info("threads: the first SuspendThread -- tid %u %s.\n", t->tid,
                t == g_self ? "suspended ITSELF and is now waiting to be "
                              "resumed"
                            : "was suspended by another thread");
  if (t == g_self)
    guest_suspend_point();
  return previous;
}

/* ResumeThread returns the previous suspend count and removes one level. */
int guest_thread_resume(uint32_t handle) {
  GuestThread *t = by_handle(handle);
  int was;
  if (!t) {
    g_resume_unknown++;
    return -1;
  }
  was = t->suspended;
  if (t->suspended > 0)
    t->suspended--;
  g_resumes++;
  t->n_resume++;
  if (!was && ++g_resume_noop == 100000ul)
    x2_log_error("threads: ResumeThread has been called %lu times on tid %u "
                 "(start 0x%08x), which was NOT suspended on any of them -- so "
                 "every one was a no-op.\n"
                 "  Something is spinning on it while waiting for something "
                 "else. Reported once, at the hundred-thousandth.\n",
                 g_resume_noop, t->tid, t->start);
  if (g_resumes == 1) {
    x2_log_info("threads: the first ResumeThread -- tid %u, previous suspend "
                "count %d%s.\n",
                t->tid, was,
                was > 1    ? " (one nested suspend remains)"
                : was == 1 ? " (now runnable)"
                           : " (already runnable; no change)");
  }
  if (was == 1)
    guest_cond_broadcast();
  /*
   * AND NOTHING ELSE -- deliberately. Win32's ResumeThread makes the target
   * runnable and returns; it does not wait, and neither does this. The
   * resumed thread competes for the guest mutex when the caller next yields,
   * which the quantum guarantees happens.
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

void guest_thread_exit(uint32_t code) {
  GuestThread *t = g_self;
  if (!t || t->is_main) {
    x2_log_error("threads: _endthreadex on the MAIN thread, which Win32 "
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
  guest_unlock();
  pthread_exit(NULL);
}

void guest_thread_report(void) {
  int i, live = 0;
  for (i = 0; i < MAX_THREADS; i++)
    if (g_thread[i].used && !g_thread[i].finished)
      live++;
  if (!g_created) {
    x2_log_info(
        "  threads: no guest thread was ever created; everything ran on "
        "the main thread.\n");
  } else {
    x2_log_info(
        "  threads: %lu created, %lu exited, %lu reaped (handle closed, "
        "stacks freed), %d still running; %lu suspend(s), %lu resume(s)\n",
        g_created, g_exited, g_reaped, live, g_suspends, g_resumes);
    if (g_resume_noop)
      x2_log_info("         %lu resume(s) were of a thread that was NOT "
                  "suspended -- Win32 no-ops those, and a loop doing them is "
                  "waiting for something else.\n",
                  g_resume_noop);
    if (g_resume_unknown || g_suspend_unknown)
      x2_log_info("         %lu resume(s) and %lu suspend(s) named NO live "
                  "thread -- a handle whose thread had already been reaped. A "
                  "loop doing that is waiting for something that cannot "
                  "happen.\n",
                  g_resume_unknown, g_suspend_unknown);
    for (i = 0; i < MAX_THREADS; i++) {
      GuestThread *t = &g_thread[i];
      /* Reaped slots are printed too, until they are reused: their
         counters are the only record of where a spin loop's resumes
         went, and skipping them is what made 9,000,634 of them
         invisible. */
      if (!t->used && !t->tid)
        continue;
      x2_log_info("         tid %u  start 0x%08x  %lu suspend(s) %lu "
                  "resume(s) %lu turn(s)%s%s\n",
                  t->tid, t->start, t->n_suspend, t->n_resume, t->n_ran,
                  !t->used      ? "  REAPED"
                  : t->finished ? "  EXITED"
                                : "",
                  t->suspended ? "  SUSPENDED NOW -- if the run stalled, this "
                                 "is a thread waiting for a ResumeThread that "
                                 "never came"
                               : "");
    }
  }
  /* Flushed, because this now runs from the abort paths too and an unflushed
     stdout buffer is discarded by abort() -- the report was written, and
     vanished, on exactly the stall it exists to explain. */
  /*
   * Printed even when they are ZERO, with their denominators. "0
   * preemptions" and "preemption is not compiled in" are different facts and
   * a line that only appears when the number is non-zero cannot tell them
   * apart -- and zero here is itself the answer to "why did two spinning
   * threads not take turns".
   */
  x2_log_info(
      "         %lu condition/mutex hand-off(s), %lu of them preemptions at a "
      "quantum of %lu boundary crossing(s)%s\n",
      g_switches, g_quanta, g_quantum,
      g_quanta ? ""
               : " -- NO preemption happened: either no second guest "
                 "thread was ever runnable, or the quantum is larger "
                 "than this run");
  x2_log_info("         the guest mutex was contended %lu time(s)\n",
              g_contended);
}
