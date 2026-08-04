---
id: C029
kind: claim
status: holds
created: 2026-08-05
tags: 
---

## Claim

The recompiled exe's hang is localised: execution enters ORIGINAL code via the hybrid fallback at 0x0065adcc (in the CRT, adjacent to the CPUID feature-detection routine) and never returns. No recompiled function runs afterwards.

## Evidence

direct.log ends with 'x86_fallback: 0x00656745' then 'x86_fallback: 0x0065adcc has no recompiled body -- running ORIGINAL code' and nothing further across the remaining ~55 seconds. The watchdog now writes to a file and flushes each line -- the earlier stderr version produced nothing because a timeout-killed process loses buffered output -- and it logs 'started' and then NOTHING, so not even the 3-second tick fires.

## What would falsify it

The watchdog's silence is UNEXPLAINED and undercuts the rest: a separate thread should still tick even if the main thread is stuck, so either the whole process is wedged (a lock held across the fallback) or the thread never really ran. Until that is understood, 'execution does not return from 0x0065adcc' is an inference from an absence of output, not an observation. The obvious suspect is x86_call_host switching ESP to the guest stack around a call that does not return normally, but that has NOT been tested.
