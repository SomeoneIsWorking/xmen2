---
id: 57
title: The movie rendezvous deadlocks about one run in six: the decoder is suspended and the only thread that resumes it is waiting
status: open
symptom: kernel32: WaitForSingleObject(INFINITE) on event (unnamed) has waited 30 seconds; the guest executed NOTHING in the last 5.0s; one libCriMovie thread SUSPENDED NOW
tags: threads,movie,deadlock,intermittent,pc,native
created: 2026-08-12
updated: 2026-08-12
---

## Symptom

Intermittent, roughly one run in six at 150 seconds (0/5 at 100s, 1/3 at
150s, 2/2 on two 300s runs). The run stops during the intro movies:

    kernel32: WaitForSingleObject(INFINITE) on event "(unnamed)" has waited 30 seconds
      threads: 9 created, 6 exited, 6 reaped, 3 still running; 1035 suspend(s), 18001223 resume(s)
             tid 1008  start 0x25002630  1 suspend(s) 1 resume(s)  SUSPENDED NOW
    [HB] the guest executed NOTHING in the last 5.0s (crossings unchanged)

## What is established

* The stalled state is: the MAIN thread blocked in `WaitForSingleObject`
  (INFINITE) on an unnamed event, the libCriMovie decoder thread
  (start 0x25002630) SUSPENDED, and the other two movie threads blocked. No
  guest code runs at all.
* In a HEALTHY run the same decoder is resumed constantly:
  **54,000,534 of 54,003,175 ResumeThreads are no-ops on a thread that was
  not suspended** -- the main thread spins on ResumeThread while it waits for
  the decoder. That is the game's normal pattern, now counted.
* So the deadlock is an ORDERING one: the decoder self-suspends at a moment
  when the main thread has gone into the blocking wait instead of the spin,
  and the only code that would resume it is that same blocked thread.
* Not a lost handle: `g_resume_unknown` is 0, so every one of those resumes
  named a live thread. This is NOT a recurrence of issue #50.
* `PulseEvent on "(unnamed)" with NOBODY waiting -- the pulse is lost` is
  reported in every run, healthy or stalled. A pulse that arrives while the
  main thread is between "decided to wait" and "registered as a waiter" is
  lost, and under this host's ONE-GUEST-THREAD-AT-A-TIME lock the interleaving
  is coarser than Windows', so the window is bigger here than it is there.

## What has NOT been established

Which event it is, who signals it, and whether the game's own protocol
tolerates a lost pulse on Windows. Naming the event (its handle, its creator,
whether it has ever been signalled) is the next measurement, and reading
libCriMovie's rendezvous is the one after that.

## Do not

Do not "fix" this by making PulseEvent hold a wakeup for a future waiter.
That changes documented semantics to paper over an ordering this host
created, and it would hide the real question, which is whether the coarse
global lock is what makes the window wide.

### Note (2026-08-12)
DEAD END, measured: making ResumeThread a HAND-OFF (broadcast, then guest_cond_wait_ms(1) so the woken thread can take the global lock and actually run) makes it WORSE, not better. With it, the story cutscene stopped presenting entirely after ~180s while boundary crossings went from ~100M per 45s to 4.3 BILLION per 45s -- a livelock: the lock is handed to a thread that also spins without blocking, and the main thread never gets it back. Reverted. What that measurement says about the design: BOTH sides of this rendezvous spin without blocking, so no amount of hand-off-at-a-syscall fixes it -- a one-guest-thread-at-a-time model cannot schedule two spinners. The candidate design is preemption by QUANTUM (release the lock every N boundary crossings) rather than at named syscalls, and that is a real change to the threading model, not a tweak. The starvation is also what makes the cutscene play at 1.7 frames a second (654 real resumes over ~380 seconds = one decoder slice per 590ms).

### Quantum preemption, implemented and MEASURED -- but NOT shown to fix this
The candidate design named above now exists: `guest_quantum()` in
`src/native/threads.c`, called from the dispatch boundary in
`x86_native_call_at` every `X2_QUANTUM` crossings (default 20,000; `X2_QUANTUM=0`
disables it and is the control). If another guest thread is blocked on the
global lock, the running thread drops it, calls `sched_yield()` and takes it
back. One guest thread still executes at a time, so nothing built on that
invariant changes -- only *which* one, and how often that can change.

It fires: 216 preemptions in the first 90 s of a run, 3,413 over a 300 s
gameplay run. The gameplay path is unaffected (no abort, presents keep
climbing, ~1,000 per 60 s).

**It is NOT established that this fixes the deadlock, and nobody should record
that it does without a run sample.** Two measurements argue for caution:

* The lock is contended only **19 times in 140 s**. A thread parked in
  `pthread_mutex_lock` counts as one contention and then sits there, so that
  number does not by itself mean "no thread is ever waiting" -- but combined
  with the preemption count (3,413 fired out of roughly 33,000 quantum
  expirations, so `g_waiters > 0` about 10% of the time) it says most of the
  time the other guest threads are NOT blocked on the lock at all. They are in
  `guest_cond_wait`, waiting to be signalled. A thread waiting for a signal is
  not helped by a preemption.
* This issue is intermittent at about 1 run in 6. Distinguishing that from zero
  needs a sample of runs, not one run that happened to survive.

So the honest state: the mechanism the earlier analysis asked for now exists and
is instrumented, and the premise it was based on ("both sides spin without
blocking, neither ever reaches a release point") is only partly borne out. The
next step is a run sample with `X2_QUANTUM=0` against the default -- and, before
that, finding out what the OTHER thread is actually waiting for, because
`guest_cond_wait` is where the evidence points and a lost `PulseEvent` (already
reported by this run, "with NOBODY waiting") is a better suspect than the
scheduler.
