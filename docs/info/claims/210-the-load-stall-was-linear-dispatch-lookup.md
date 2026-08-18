---
id: C210
kind: claim
status: holds
created: 2026-08-17
tags: performance,dispatch,load,faster-loading
---

## Claim

The level-load stall was the runtime's LINEAR dispatch-table lookup, not the guest's geometry building: `find()` scanned each module's ~6000-entry table on every dispatch, and binary search over a now-SORTED table (emitted by recomp.py native) cuts the load frame from 4592 ms to ~500 ms (9.2x) with identical dispatch resolution

## Evidence

X2_PROFILE (a new sampling profiler, see C211) showed the load frame's top sampled body was `igArenaMemoryPool::isActive` at 15.6% — a TWO-instruction function (`MOV AL,[ECX+0x74]; RET`) reached ONLY via vtable dispatch, so its share was pure dispatch/call overhead on a hot allocation path. Root cause: `find()` in src/native/x86rt_native.c was a LINEAR scan of the module's function table on EVERY dispatched call. The load window dispatches ~460k times/frame (the level-build frames), libIGCore's table is ~5900 entries, so ~1.3 G compares/frame. `recomp.py native` emitted the table in JSON order with interior entries appended, and registered `nfns = len(functions)`, so the table was not only unsorted but the registered length excluded the interior entries.

Fixes: (1) `recomp.py native` emits the table SORTED by entry point (functions + interior entries merged); (2) `nfns` is now the full table length `len(functions) + len(interior)` (the old value excluded interior entries entirely, and binary search bounded by it MISSED real entries at indices past nfns -- observed as a dispatch to 0x103ed28c in msdia80 at table index 4851 with nfns=4848, aborting on "no recompiled body"); (3) `find()` is a binary search (log2(6000)=13 compares). Verified: all 20 modules' tables sorted, `nfns` matches table length everywhere, and a simulated binary-vs-linear comparison agrees on every EP and gap across every module. The boot-direct load frame fell from 4592 ms to 499-587 ms (frame 1, "the rest is guest logic"), and the boot-direct path (X2_BOOT_MAP) runs 300+ frames cleanly.

## What would falsify it

a load window where isActive or another 2-instruction vtable body is NOT near the top of the sampler after this change, or a re-measurement of the load frame that is not ~9x faster, or a dispatch table that binary search disagrees with linear search on (checked -- none)