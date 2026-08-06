---
id: C088
kind: claim
status: holds
created: 2026-08-06
tags: pc,recomp,native,memory,rc-exe
---

## Claim

The unbounded arena walk is a heap-grow loop inside libIGCore's own statically-linked MSVC CRT, not repeated pool creation

## Evidence

Call COUNTS from the reached set (I021) on scratch/build-reached, one run: igMemoryPoolContext::bootstrapMemoryPoolInitialization x1, igMemoryPool::operator_new x1, igArenaMemoryPool ctor x1, igArenaMemoryPool::bootstrapInit x1, setPreSize x1, igArenaSystemMalloc x1, arenaAllocate x2 -- every engine-level function on the path runs ONCE or twice. Meanwhile libIGCore's CRT heap-grow function FUN_1006aa50 (one of only two VirtualAlloc call sites in libIGCore, the other FUN_1006ae10 is NEVER entered) runs x28, and its helper FUN_10066730 x27. So a SINGLE igArenaSystemMalloc produced ~527 MB of reservations: the loop is beneath the engine, in the CRT heap, which grows and grows without ever satisfying the request. igMemoryPool::addMemoryPool is NEVER reached, consistent with the run dying before any pool is registered.

## What would falsify it

The x52 count for FUN_1006a500 and x27 for FUN_10066730 are AMBIGUOUS: those linked addresses exist in both libIGCore and libIGSg (both linked for 0x10000000) and the reached set keys on the linked address, so those two counts may be a sum across modules. FUN_1006aa50 x28 is unambiguous (libIGCore only). If per-module counts show the CRT loop is actually in libIGSg, this localisation is wrong.
