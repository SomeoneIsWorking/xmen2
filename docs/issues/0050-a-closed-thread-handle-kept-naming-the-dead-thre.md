---
id: 50
title: A closed thread handle kept naming the dead thread, so every ResumeThread woke a corpse
status: resolved
symptom: The intro movie plays once and then the game stalls forever: the main thread waits on an event, the new decoder threads sit CREATE_SUSPENDED with zero resumes, and the dead thread has 9,000,634 of them
tags: pc,native,threads,kernel32,handles,libCriMovie,movie,root-cause
created: 2026-08-11
updated: 2026-08-11
---

## The symptom, and why it read as a deadlock

The first movie played to completion. The next one created its three decoder
threads and they were never started: the main thread waited on a manual-reset
event for a frame that could not arrive, at a steady 60 Hz tick, forever. Every
surface reading said "the movie player's rendezvous does not work under one
global lock" (issue #49).

The per-thread accounting is what broke it open, and only because the TOTALS
were nonsense:

```
threads: 6 created, 3 exited; 43 suspend(s), 3000045 resume(s)
   tid 1002  603 suspend(s) 9000634 resume(s)  EXITED
   tid 1005    0 suspend(s)       0 resume(s)  SUSPENDED NOW
```

Nine million resumes on a thread that had already exited, and none at all on
the live one that needed them.

## Root cause

kernel32 hands out small table indices as handles and `CloseHandle` frees the
slot, so the NEXT `_beginthreadex` is given the same number. `threads.c` kept
`t->handle` for ever, and `by_handle()` scans in creation order -- so it
matched the DEAD thread first. `ResumeThread` woke a corpse (returning "was not
suspended", which is true and useless), and the new decoder stayed suspended
for the rest of the run.

## The fix

`CloseHandle` on an `H_THREAD` calls `guest_thread_handle_closed()`, which
clears the association. A finished thread is also REAPED there -- its 1 MB
stack and its TIB go back to the guest heap, which matters because three
threads are created per movie and the run plays six.

## Verified

18 threads created, 18 exited, 18 reaped, 0 still running: **six movies play
through in sequence**, at ~50 presents per second, and the run continues past
them into the exe's own code (a missing body at `XMen2.exe 0x0049fb00`, which
is ordinary discovery-loop work). Before the fix the run stalled after the
first movie with the guest executing 13,500 crossings per 5 s and presenting
nothing; after it, 27,000,000 crossings per 5 s.

## What this cost, and the instrument that hid it

Three separate wrong diagnoses, each of which looked well-evidenced:

* "the rendezvous is serialised away by the global lock" (issue #49) -- the
  threading model's own stated caveat, which fitted perfectly and was wrong.
* "PulseEvent loses a wakeup" -- plausible, and disproved by its own report:
  the "nobody was waiting" line never fired.
* "FUN_10002a70 never returns" -- from the argument watch printing an entry
  with no matching exit. It DID return: these functions end in a TAIL CALL, so
  the exit is attributed to the callee. Silence from the watch was read as
  "still inside", and that is now a recorded failure mode of I027.

What settled it was `gdb -p` on the stuck process: two threads parked at
`thread_main`'s CREATE_SUSPENDED wait, the main thread in `WaitForSingleObject`
with a 16 ms timeout. This is a native ELF -- a real debugger works on it, and
reaching for one earlier would have been quicker than three rounds of static
reading.
