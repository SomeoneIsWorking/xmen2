---
id: 15
title: Native run: igArenaMemoryPool::consolidate dereferences NULL
status: open
symptom: SIGSEGV at (nil) inside Gap::Core::igArenaMemoryPool::consolidate (libIGCore 0x10057dc0), reached from igArenaMemoryPool::malloc -> memoryOperation. Occurs AFTER the memory-pool bootstrap now completes (issue #14 fixed)
tags: pc,recomp,native,memory,engine-init,rc-exe
created: 2026-08-06
updated: 2026-08-06
---

## Where it stops

This is the frontier that issue #14's fix exposed. The memory-pool bootstrap now
completes: `igMemoryPool::initBootstrap` runs, `trimAll` is never called, and the
arena pool services real malloc/free traffic with statistics
(`updateLocalStatistics` / `updateUserStatistics` in the boundary ring).

    addr2line ->  fn_libIGCore_10057dc0  =  Gap::Core::igArenaMemoryPool::consolidate

The last crossings before the fault are a repeating
malloc -> memoryOperation -> updateUserStatistics -> updateLocalStatistics cycle,
then `consolidate`.

## Not yet established

Whether this is a translation defect in `consolidate` (it walks a free list, so a
mistranslated pointer update would show up exactly like this), or a real NULL the
engine would also see because something earlier handed it a pool in a state the
stock build never produces. The stock build under Wine is the control and has not
been compared here.

## Next

1. `X2_REACHED` the free-list path around `consolidate` for call counts -- a
   consolidate that runs once and dies differs from one looping over a corrupt list.
2. `X2_PEEK` the arena descriptor at the fault to see which pointer is NULL.
3. Compare against the stock build under Wine before assuming the translation is
   at fault.
