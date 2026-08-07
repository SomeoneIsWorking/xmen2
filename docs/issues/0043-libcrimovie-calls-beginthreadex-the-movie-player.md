---
id: 43
title: libCriMovie calls _beginthreadex: the movie player wants a real thread
status: open
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
