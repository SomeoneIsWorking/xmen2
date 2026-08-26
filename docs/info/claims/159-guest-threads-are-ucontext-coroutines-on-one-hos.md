---
id: C159
kind: claim
status: holds
created: 2026-08-12
tags: threads,scheduling,macos,arm64
depends: src/native/threads.c#guest_quantum, src/native/threads.c#guest_cond_wait_ms
---

## Claim

Guest threads are pthreads serialized by one mutex and parked with condition variables; the preemption point remains in every recompiled body (`X86_ENTER_FN`) rather than only at the dispatch boundary

## Evidence

Measured on native arm64 macOS with `X2_MAX_FRAMES=500`: the game reached the requested 500-frame stop and exited zero, created all three libCriMovie workers, made 820 condition/mutex hand-offs, including 339 preemptions at the 20,000-boundary quantum, and contended the guest mutex 87 times. Nested `SuspendThread`/`ResumeThread` counts are exact and a self-suspend parks on the condition variable. The complete 106-test suite passes (one asset-dependent media test skips).

## What would falsify it

A run whose per-thread heartbeat shows one thread holding the guest turn for seconds while another remains runnable, a lost wakeup after `ResumeThread`, two pthreads executing translated guest code simultaneously, or a movie stall at the same bounded frame run.
