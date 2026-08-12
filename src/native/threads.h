/*
 * Guest threads.
 *
 * See threads.c for the whole design. The one thing a caller needs to know
 * here: exactly ONE guest thread executes at a time, under a global lock, and
 * that lock is released only at the points named in threads.c.
 */
#ifndef X2_THREADS_H
#define X2_THREADS_H

#include <stdint.h>

/* Bookkeeping, not a mutex: guest threads are coroutines on ONE host thread,
   so there is nothing to lock. The depth is counted because preemption must
   not happen from a nested hold. The call sites are unchanged and still mark
   exactly the right places. */
void guest_lock(void);
void guest_unlock(void);

/*
 * A preemption point: if another guest thread is waiting for the lock, drop
 * it, let the scheduler choose, and take it back. Called from the dispatch
 * boundary every `quantum` crossings -- see threads.c for why a model that
 * only releases at syscalls cannot schedule two spinning threads.
 */
void guest_quantum(void);
void guest_quantum_configure(unsigned long crossings);
void guest_quantum_from_env(void);
unsigned long guest_quantum_size(void);
unsigned long guest_quantum_count(void);

/*
 * The running guest thread's id, in the GUEST's numbering -- not the host's.
 * GetCurrentThreadId returns this, and a critical section's owner field holds
 * it, so the two agree by construction. The main thread has one too.
 */
uint32_t guest_current_tid(void);

/* SetThreadPriority/GetThreadPriority: recorded per thread and round-tripped.
   The schedule is round-robin and a priority cannot change it, but Get must
   return what THIS thread set. */
void    guest_thread_priority_set(int32_t p);
int32_t guest_thread_priority_get(void);

/*
 * One line per live guest thread: what it is blocked on and for how long.
 * Printed from the heartbeat -- a stall has to be watched while it happens,
 * and a shutdown report arrives after the kill that ended the argument.
 */
void guest_thread_state_report(void);

/*
 * Mark a blocking HOST call, so the heartbeat can name what a stalled thread
 * is in. It cannot let another guest thread run -- one host thread runs them
 * all -- so a host call that blocks blocks everything, and that is stated here
 * rather than implied. Guest waits go through guest_cond_wait_ms and Sleep
 * through guest_sleep_ms; both yield properly.
 */
void guest_blocking_begin(void);
void guest_blocking_end(void);

/*
 * Wait: park this guest thread and let the scheduler run another. Returns when
 * something broadcasts or the deadline passes. ms of 0xFFFFFFFF is INFINITE.
 */
void guest_cond_wait_ms(uint32_t ms);

/*
 * Sleep. Sleep(0) gives up the rest of this thread's turn, which is what Win32
 * means by it; anything longer parks with a deadline so the other guest
 * threads run for its duration instead of the whole process stopping.
 */
void guest_sleep_ms(uint32_t ms);

/* Wake everything waiting. Called whenever a guest-visible object is
   signalled -- a thread exiting, an event set, a semaphore released. */
void guest_cond_broadcast(void);

/*
 * _beginthreadex's host half. Returns a HANDLE (a kernel32 handle-table index)
 * or 0. `stack_bytes` of 0 means the default.
 */
uint32_t guest_thread_create(uint32_t start, uint32_t arg, uint32_t stack_bytes,
                             uint32_t *tid_out);

/* CREATE_SUSPENDED: the thread exists and has NOT entered the guest routine
   until guest_thread_resume is called. */
uint32_t guest_thread_create_ex(uint32_t start, uint32_t arg,
                                uint32_t stack_bytes, int suspended,
                                uint32_t *tid_out);

/* -1 if the handle names no guest thread, else the previous suspended flag. */
int guest_thread_resume(uint32_t handle);

/*
 * SuspendThread. -1 if the handle names no guest thread, or if that thread is
 * already suspended (Win32 would COUNT the second suspend; this host has one
 * flag and refuses rather than miscounting). Otherwise the previous count, 0.
 *
 * Suspending the CALLING thread BLOCKS until something resumes it, which is
 * the case the movie player needs: its decoder parks itself between frames.
 */
int guest_thread_suspend(uint32_t handle);

/* Whether a handle names a live guest thread, and its exit code once it is
   not. Used by WaitForSingleObject (a thread handle signals when it exits) and
   by GetExitCodeThread. */
int  guest_thread_is_thread(uint32_t handle);
int  guest_thread_finished(uint32_t handle, uint32_t *exit_code);

/* Block until that thread exits, or until `ms` elapses. 1 if it exited. */
int  guest_thread_join(uint32_t handle, uint32_t ms);

/*
 * CloseHandle on a thread handle. kernel32 REUSES handle numbers, so a record
 * that kept its handle for ever would be matched by a later thread's handle --
 * see the note in threads.c, and issue #50. A finished thread's stack and TIB
 * are returned to the guest heap here.
 */
void guest_thread_handle_closed(uint32_t handle);

/* The current thread's exit, from _endthreadex. Does not return. */
void guest_thread_exit(uint32_t code);

/* How many were created, how many are still running, and whether the lock was
   ever actually contended -- a threading model nothing contends is one that
   has never been exercised. */
void guest_thread_report(void);

/* The ENGINE's own thread list, read out of guest memory -- see issue #61.
   Says which of its two candidates the run is in, rather than leaving them
   to be argued about. */
void guest_engine_thread_report(void);

#endif /* X2_THREADS_H */
