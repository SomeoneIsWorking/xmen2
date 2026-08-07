---
id: 42
title: libCriMovie needs WINMM timeSetEvent -- a multimedia timer that calls back into guest code
status: investigating
symptom: x86_missing_import: WINMM.dll!timeSetEvent is not implemented natively -- reached when the game starts its intro movie
tags: pc,native,winmm,threads,libCriMovie,movie
created: 2026-08-07
updated: 2026-08-07
---

## What is being asked for

`timeSetEvent(delay, resolution, callback, user, flags)` runs a GUEST callback
on a timer thread. libCriMovie -- the intro movie player -- imports it along
with `timeKillEvent`, `timeBeginPeriod` and `timeEndPeriod`. The last two are
implemented (granted: this host's timers are nanosecond-resolution already, so
the guarantee the caller asked for is one it already has).

## Why it is not a small stub

This runtime executes recompiled bodies on ONE thread and the CPU state is a
plain struct passed down by pointer. A second thread entering a body is a data
race on the register file itself, so "start a thread and call the callback" is
not a five-line implementation -- it is a threading model.

Two shortcuts, both worse than stopping:

* **A fake timer id.** The callback never fires, whatever it drives never
  happens, and the caller believes it has a timer. Silent.
* **Return 0 (failure).** Truthful about the timer, and it sends libCriMovie
  down an error path for a reason that has nothing to do with the movie.

## The three real options

1. **A deferred callback pumped from the guest's own thread** -- queue the due
   callbacks and run them at an existing per-frame host callback (Present).
   No race, no new thread; the resolution is one frame instead of the
   millisecond that was asked for. Whether the movie player tolerates that is
   the open question, and it is cheap to find out.
2. **A real guest thread**, which is the honest general answer and a much
   bigger piece of work: per-thread CPU state, and every host import that keeps
   static state becomes a synchronisation question.
3. **Skip the movie.** The engine has its own path for a missing movie; if the
   port is not playing FMV yet, declining CriMovie outright is more honest than
   half a timer -- and it should be declined where the movie is REQUESTED, not
   by breaking a timer it happens to use.

Option 1 first, because it is the only one that is both cheap and answerable
by experiment.


## Done: option 1, and it is UNVERIFIED

`timeSetEvent`/`timeKillEvent` are implemented as DEFERRED callbacks that run
on the guest's own thread, pumped from `QueryPerformanceCounter` and `Sleep` --
the two places a loop waiting for a timer reaches constantly. No thread, no
race on the register file.

The cost is stated in the code and in the exit report rather than left to be
found: **the resolution is the poll interval, not the millisecond that was
asked for**, and a callback the guest never reaches a pump point for never
fires. `winmm_report` prints the average lateness and names any timer that has
never fired at all.

**It has never run.** libCriMovie asks for a THREAD before it ever sets a
timer, so the run stops earlier now and the report says exactly that:

    winmm: no multimedia timer was ever set.

That is the honest status: the code exists, its negative reports itself, and
nothing has exercised it. It is not evidence that deferred timers work for
libCriMovie -- issue #43 has to be answered before that can be tested at all.
