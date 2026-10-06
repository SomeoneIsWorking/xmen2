---
id: 49
title: The movie player's threads rendezvous, and one global lock serialises the rendezvous away
status: resolved
symptom: The run stalls after the intro movie starts: WaitForSingleObject(INFINITE) on an unnamed event waits 30 seconds and nothing signals it, while a libCriMovie thread sits in a SuspendThread it called on itself
tags: pc,native,threads,libCriMovie,movie,architecture,synchronisation
created: 2026-08-11
updated: 2026-08-11
---

## Where the run is now

`SuspendThread` is implemented (issue #42/#43 lineage), so the run no longer
stops at a missing import. It stops one step further, and the stop is
BEHAVIOURAL rather than a missing surface:

```
threads: the first GUEST THREAD is running (start 0x25002590, 1 MB stack)
threads: the first ResumeThread -- tid 1000, which was suspended and will now continue.
threads: the first SuspendThread -- tid 1002 suspended ITSELF and is now waiting to be resumed.
kernel32: WaitForSingleObject(INFINITE) on event "(unnamed)" has waited 30 seconds
          and nothing has signalled it.
  threads: 3 created, 0 exited, 3 still running; 1 suspend(s), 3 resume(s)
         tid 1002 is SUSPENDED and was never resumed
```

## What the guest code does

libCriMovie `FUN_10002630` is the decoder loop, and it is a PARK, not a kill:

```
loop:  [0x100572bc]++
       if (FUN_10008600() && [0x100572b0] != 1) goto check
       if ([0x100572b0] == 1) { [0x100572b0] = 0; SetThreadPriority(own, prio) }
       if ([0x100572c4]) [0x100572c4]([0x100572c8])      ; the per-frame callback
       SuspendThread([0x1014a1fc])                        ; its OWN handle
check: if (![0x100572e4]) goto loop
```

`[0x10042058]` is SuspendThread's IAT slot and `[0x10042070]` is
SetThreadPriority's; the handle at `[0x1014a1fc]` is the thread's own. Four
functions call ResumeThread through `[0x1004206c]` -- `FUN_10002520`,
`FUN_10002590`, `FUN_100026f0`, `FUN_10002bb0` -- so the wake path exists.

Meanwhile the main thread is inside `FUN_10002910` (reached from
`0x10001c21`), which waits on an event.

## The hypothesis, NOT yet confirmed

This host runs one guest thread at a time under a global lock released only at
Sleep and at waits (`src/native/threads.c` says so in its own header). The
decoder therefore does not run CONCURRENTLY with the main thread: it takes the
lock as soon as the creator yields, runs its loop to the park, and only then
does the main thread continue -- and by then the thing it waits for was
supposed to be produced by a decoder that is now parked.

That is the caveat the threading model already states ("a title that expects a
decoder to keep up while the main thread spins would starve it"), arriving in
practice.

## What has NOT been established

* WHICH event the main thread waits on, and which thread sets it. The report
  says "(unnamed)"; the creating call site is not recorded.
* whether the ordering is the problem at all, or whether a step BEFORE this
  (the deferred multimedia timer of issue #42, which has still never fired)
  is what should have driven the handshake.

Both are cheap to answer and neither has been done, so nothing here should be
treated as diagnosed.

## Options, when it is

1. Record who CREATED each sync object, so "(unnamed)" names a call site. This
   is instrumentation, and it is the first thing to do.
2. Narrow the lock around libCriMovie specifically, with evidence -- the
   threading model's own stated escape hatch, and the one that must not be
   taken on a guess.
3. Decline CriMovie where the movie is REQUESTED (issue #42, option 3). The
   engine has a path for a missing movie, and the port does not need FMV to
   reach gameplay.

### Resolution (2026-08-11)
NOT the threading model. The rendezvous works: the cause was a stale thread-handle association (issue #50) -- kernel32 reuses handle numbers and threads.c kept the old one, so every ResumeThread aimed at a new decoder woke the previous movie's dead thread. With that fixed, six movies play through in sequence at ~50 presents/s and the run continues into the exe's own code. Three things were built while chasing this and all three are keepers: the multimedia timers are pumped from inside a blocking WAIT (a thread blocked there reaches no other pump point, so a wait for something a timer callback produces waited forever), the wait sleeps until the next timer is DUE rather than a flat second (which alone took the movie from 1.3 to 40 fps), and PulseEvent is implemented -- exactly, including the manual-reset case as a pulse GENERATION so it releases every thread waiting at that instant and no later one.
