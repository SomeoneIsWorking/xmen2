---
id: C087
kind: claim
status: holds
created: 2026-08-06
tags: pc,recomp,native,engine-init,memory
---

## Claim

The recompiled exe's SIGSEGV at 0x4 is an UNBOUNDED arena-growth loop inside igMemoryPoolContext::bootstrapMemoryPoolInitialization, not a missing bootstrap

## Evidence

Measured with the reached set (I021) and X2_PEEK (I022) on scratch/build-reached. (1) bootstrapMemoryPoolInitialization IS reached (first entry #1322 of 1358) -- issue #14's open question. (2) igUseLegacyMemoryPools is correctly TRUE at the byte libIGCore reads: XMen2.exe!FUN_00403420 sets it at #1309, its IAT slot holds 0x2415f3fc, and a peek of libIGCore+0x15f3fc reads 0x01 -- so the flag, the binding and the ordering are all right. (3) The order of first entry is bootstrap #1322 -> getCurrentMemoryPool #1324 -> igMemoryPool::operator_new #1329 -> igArenaMemoryPool ctor #1330 (so the pool object allocation SUCCEEDS) -> igArenaSystemMalloc #1348 -> arenaAllocate #1349 -> trimAll #1358, which faults. So initBootstrap NEVER running is a CONSEQUENCE: control dies inside bootstrap's iteration 0 a few instructions before its call at 0x1003da0d, and trimAll at 0x1003b540 dereferences _RawMemMemoryPoolList (libIGCore+0x15f3f0, still 0x00000000) with no NULL check -- MOV EAX,[0x1015f3f0] then [EAX+4] is exactly the fault at 0x4. (4) trimAll is only reached because the virtual at igArenaSystemMalloc+0x10054cf6 returned -1, i.e. the OOM path. (5) The allocation loop is UNBOUNDED, measured: VirtualAlloc call count scales linearly with the advertised budget -- 34 calls at X2_PHYS_MB=256, 67 at 512, 132 at 1024 -- reserving ~19.5 MB per arena until the budget refuses. This is why 512/1024/1536 MB all gave the identical failure: raising the budget only moves the wall.

## What would falsify it

If the stock PC build under Wine also reserves memory until the OS refuses during engine startup, then the walk is the engine's normal strategy and the defect is only trimAll's missing NULL check -- measure VirtualAlloc calls in the Wine oracle before assuming the loop is ours.
