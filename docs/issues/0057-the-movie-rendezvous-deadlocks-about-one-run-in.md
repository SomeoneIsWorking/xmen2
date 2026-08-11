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
