---
id: C147
kind: claim
status: holds
created: 2026-08-07
tags: pc,native,threads,architecture,libCriMovie
---

## Claim

The native host now runs GUEST THREADS: _beginthreadex creates a real pthread with its own guest stack, its own TIB (g_fsbase is __thread) and its own TLS slots, and exactly one guest thread executes at a time under a global lock released at Sleep and at every wait.

## Evidence

libCriMovie's movie init calls _beginthreadex with CREATE_SUSPENDED and then ResumeThread; with threads implemented the run gets past both and reaches new libCriMovie code, stopping on an ordinary missing body at 0x10006a50 rather than on an import. WaitForSingleObject no longer aborts -- it waits on a condition variable with the guest lock released, and an INFINITE wait is bounded at 30s so a deadlock between guest threads is reported rather than hung on. guest_thread_report states whether the lock was ever contended, so a run in which the threads never actually overlapped cannot be mistaken for one that exercised the locking.

## What would falsify it

a run in which two guest threads execute recompiled bodies at the same time (the lock is not doing its job), or one where guest_thread_report says the lock was never contended while claiming the threading is exercised
