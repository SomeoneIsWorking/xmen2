---
id: C159
kind: claim
status: holds
created: 2026-08-12
tags: threads,scheduling
---

## Claim

Guest threads are ucontext coroutines on ONE host thread, and the preemption point is in every recompiled body (X86_ENTER_FN) rather than at the dispatch boundary

## Evidence

smoke_loop closes the loop on 5 of 6 coroutine runs (the sixth aborted on a check that has since been corrected to report instead). 470k-737k coroutine switches and 12k-44k preemptions per run. Critical sections went from 0 contended enters under pthreads to 555-706 of ~660k, so guest threads now genuinely overlap inside them. The first coroutine build stalled at frame 2021 with '[HB] MAIN tid 999: runnable, waiting its turn for 132.1s' while one decoder ran uninterrupted -- the quantum was counted at the dispatch boundary, which a direct guest-to-guest spin (libCriMovie FUN_10002e80/FUN_100085e0) never reaches.

## What would falsify it

A run whose per-thread heartbeat shows one thread 'running guest code' for seconds while another reads 'runnable, waiting its turn' -- that would mean X86_ENTER_FN's budget is not reaching guest_quantum. Or a movie stall whose thread states differ between two runs of the same frame-scheduled script, which would mean the schedule is not in fact ours.
