---
id: 43
title: libCriMovie calls _beginthreadex: the movie player wants a real thread
status: resolved
symptom: x86_missing_import: MSVCRT.dll!_beginthreadex is not implemented natively; the ring shows libCriMovie FUN_10002a70 calling it from 0x10002a95 during movie init
tags: pc,native,threads,libCriMovie,movie,architecture
created: 2026-08-07
updated: 2026-08-07
---

## The fork

libCriMovie's initialisation calls `_beginthreadex` BEFORE it ever sets a
multimedia timer, so the deferred-timer work in issue #42 is not enough and has
in fact never run. The movie player wants a decoding thread.

This is an architectural decision, not a missing function, and it is worth
stating both paths honestly.

### A. Guest threads

The general answer, and the one every later subsystem will also want (sound
streaming, asset loading). What it costs:

* **Per-thread CPU state.** The register file is a plain struct passed down by
  pointer; today one exists. Each guest thread needs its own, plus its own
  guest stack out of the arena.
* **Every host import with static state becomes a synchronisation question.**
  The handle table, the guest heap, the arena reservations, the D3D8 object
  table, the boundary ring -- each is currently single-threaded by assumption,
  and the assumption is invisible.
* **The renderer is not thread-safe either.** SDL_GPU command buffers belong to
  the thread that made them (which is why the headless screenshot readback runs
  on the guest's thread and not on the heartbeat's).

It is the right answer eventually. It is not a small change and it should not
be made in the middle of chasing a movie.

### B. Decline the movie

The intro FMV is not the game. Declining CriMovie where the movie is REQUESTED
-- not by breaking a thread call it happens to make -- lets the run reach the
menu, which is where the port's remaining questions are.

The engine has a path for a movie it cannot play (a title without FMV data
still boots), so the honest form of this is to answer "no movie player" at the
point the engine asks for one, and say so once.

**Recommended: B first, A when a subsystem that MATTERS needs it.** Getting to
the menu answers more questions per hour than a threading model does, and the
threading model will be designed better with the renderer and input paths
already exercised.

## What is NOT in question

`_beginthreadex` must not return a fake thread id. The thread body would never
run, the caller would wait on something that never happens, and the failure
would surface as a hang with no connection to this call. That is the same trap
the fake timer id would have been (issue #42).


## Resolved: guest threads, under ONE global lock

Option A, because every subsystem after the movie will want it.

`src/native/threads.c`. The register file is a plain struct on the C stack, so
it is already per-thread and costs nothing. Everything ELSE this host owns is
single-threaded by an assumption nobody wrote down -- the kernel32 handle
table, the guest heap's free lists, the VirtualAlloc reservation table, the
D3D8 object and resource tables, the boundary ring, the CRT's statics. So
exactly one guest thread runs at a time, under a lock released only where the
guest is not executing:

* `Sleep` -- the whole point of it is to let others run
* `WaitForSingleObject`/`WaitForMultipleObjects` -- and it MUST, or the
  signaller can never run
* joining a thread

**What it is not: parallel.** Two guest threads never execute at once, so a
title that expects a decoder to keep up while the main thread spins without
sleeping or waiting would starve it. That shows up as a stall, and the fix is
to narrow the lock around a NAMED subsystem with evidence -- not to widen it
everywhere on a guess. `guest_thread_report` says whether the lock was ever
contended at all, because a threading model nothing contends is one nothing has
exercised.

Per-thread state beyond the register file:

* **`g_fsbase` is `__thread`, and each thread gets its own TIB page.** FS:[0]
  is the SEH chain head and every recompiled prologue writes it; sharing it
  would have two threads overwriting each other's exception chain.
* **The TLS slots are `__thread`.** Per-thread is the entire meaning of the
  API -- sharing them is not an approximation, it is the opposite.
* **A guest stack each**, out of the arena.

`WaitForSingleObject` no longer aborts. It was right to: with nothing else
running, no signal could ever arrive, and both plausible answers were lies.
What changed is that something else can run. An INFINITE wait is still BOUNDED
-- 30 seconds and it reports and stops, because one guest thread deadlocking
against another has to be named rather than hung on.

Also landed, each with its reason: `_endthreadex`, `ResumeThread` and
CREATE_SUSPENDED (the thread waits before entering the guest routine, because
"suspended" has to mean "has not started"), `lstrlenA`, and
`SetThreadPriority`/`SetThreadAffinityMask` accepted-and-recorded with the
statement that under a global lock there is no scheduling to prioritise and one
CPU to be pinned to. `SuspendThread` is NOT implemented: a thread that keeps
running while the caller believes it is suspended corrupts whatever it was
protecting.

The run now reaches an ordinary discovery-loop input inside libCriMovie
(0x10006a50), which is what a subsystem being executed for the first time looks
like.
