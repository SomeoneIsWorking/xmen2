---
id: 42
title: libCriMovie needs WINMM timeSetEvent -- a multimedia timer that calls back into guest code
status: open
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
