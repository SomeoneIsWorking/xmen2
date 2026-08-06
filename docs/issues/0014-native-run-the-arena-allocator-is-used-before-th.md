---
id: 14
title: Native run: the arena allocator is used before the memory system is bootstrapped
status: resolved
symptom: SIGSEGV reading address 0x4 inside Gap::Core::igMemoryPool::trimAll, reached from igArenaMemoryPool::memoryOperation -> igArena_malloc -> igArenaSystemMalloc. The pool-list global at libIGCore+0x15f3f0 is zero
tags: pc,recomp,native,engine-init,memory,rc-exe
created: 2026-08-06
updated: 2026-08-06
---

## Where it stops

All eight modules map and initialise, the exe's CRT startup completes, and the
exe's entry point runs. It then dies in the engine's own allocator.

## What is actually wrong

**Corrected 2026-08-06 (C087). The original diagnosis below this paragraph was
wrong in its causal direction and is kept only because the correction is the
useful part.** It read "the engine's memory system has not been bootstrapped at
the point the arena allocator is used", inferring an ordering failure from the
fact that the pool list was empty. Measured with the reached set (I021), the
bootstrap IS running: it is the allocation *inside* it that never terminates.

Order of first entry, measured in one run (`X2_REACHED`, 1358 distinct entry
points entered):

    #1309  XMen2.exe!FUN_00403420                      sets igUseLegacyMemoryPools = 1
    #1322  igMemoryPoolContext::bootstrapMemoryPoolInitialization
    #1324  igMemoryPool::getCurrentMemoryPool
    #1329  igMemoryPool::operator_new                  (0xd0 bytes -- SUCCEEDS)
    #1330  igArenaMemoryPool::igArenaMemoryPool        ctor runs, so ESI is a real pool
    #1348  igArenaMemoryPool::igArenaSystemMalloc
    #1349  igArenaMemoryPool::arenaAllocate
    #1358  igMemoryPool::trimAll                       -> SIGSEGV
           igMemoryPool::initBootstrap                 NEVER

So `initBootstrap` never running is a **consequence, not the cause**: control
dies inside bootstrap's own first loop iteration, a few instructions before it
would have called `initBootstrap` at 0x1003da0d. `trimAll` then dereferences
`_RawMemMemoryPoolList` — which bootstrap has not created *yet* — with no NULL
check: `MOV EAX,[0x1015f3f0]` followed by `[EAX+4]` is exactly the fault at 0x4.

`trimAll` is only reached at all because the virtual call at 0x10054cf6 inside
`igArenaSystemMalloc` returned -1: it is the out-of-memory recovery path.

**The real defect is that the arena allocation loop is unbounded.** The
VirtualAlloc call count scales linearly with the advertised budget — 34 calls at
`X2_PHYS_MB=256`, 67 at 512, 132 at 1024 — reserving ~19.5 MB per arena until
the budget refuses. That is also why 512/1024/1536 MB gave "the identical
failure": raising the budget only moves the wall.

Ruled out as causes, each by measurement rather than by argument:

* NOT the legacy-pools flag, NOT its binding, NOT the ordering. `igUseLegacy
  MemoryPools` (libIGCore+0x15f3fc, an exported `bool` no module writes
  directly) is set by the exe through its IAT at #1309, before bootstrap at
  #1322. The exe's slot holds 0x2415f3fc and a peek of that byte reads 0x01
  (I022) — the exe's write and libIGCore's read are the same byte, with the
  right value, in the right order.
* NOT a relocation error. Both sides are base-relative against their own
  module (`g_imgbase_XMen2` / `g_imgbase_libIGCore`).

### The original, superseded diagnosis

`igMemoryPool::trimAll` reads the pool-list global at libIGCore+0x15f3f0 and
finds zero. Exactly one function writes that global:

    Gap::Core::igMemoryPool::initBootstrap        (0x1003c1f0)

and exactly one function calls it:

    Gap::Core::igMemoryPoolContext::bootstrapMemoryPoolInitialization (0x1003d900)

So the engine's memory system has not been bootstrapped at the point the arena
allocator is used. trimAll is the out-of-memory recovery path, so the sequence
is: an arena allocation fails, the allocator tries to trim the pools to make
room, and the pool registry does not exist yet.

## What was ruled out, by measurement

* NOT the advertised memory size. The game allocates until allocation fails --
  confirmed, it stops cleanly once VirtualAlloc refuses. Trying 512, 1024 and
  1536 MB gives the identical failure, so the budget is not what is missing.
* NOT a collision with the runtime. The runtime's own stack, heap and scratch
  moved to 0x70000000+ precisely because the guest's arena walk ran into the
  guest stack at 0x30000000. The collision is gone and the failure is unchanged.

## Two real bugs fixed on the way here

* VirtualAlloc returned SUCCESS on any EEXIST, reasoning that a commit over an
  existing reservation looks like that. It does -- and so does a request
  landing on the guest heap or a mapped module, which would hand the game the
  runtime's own memory. It now succeeds only over a range the guest itself
  reserved.
* GlobalMemoryStatus advertised 512 MB while VirtualAlloc enforced nothing, so
  the game took 937 MB across 120 reservations. A budget the allocator does not
  enforce is not a budget. Both now read the same figure (X2_PHYS_MB).

## The oracle settles it: the loop is ours (C087 confirmed)

The stock PC build under Wine (`WINEDEBUG=+virtual`, environment only — the
prefix registry is never modified; `scratch/logs/wine-virtual.log`) makes
exactly **four** large arena reservations during startup and then proceeds:

    reserve 0x2410000  0x1390000 = 19.6 MB   commit 0x1388000
    reserve 0x37a0000  0x1380000 = 19.5 MB   commit 0x1380000
    reserve 0x5010000  0x1000000 = 16.0 MB   commit 0x0ff2000
    reserve 0x6b40000  0x1580000 = 21.5 MB   commit 0x1579000

~77 MB, out of 126 `NtAllocateVirtualMemory` calls overall. It does **not**
reserve until the OS refuses. The native run makes 67 VirtualAlloc calls for
~527 MB. So the walk is ours.

## Localised: the loop is in libIGCore's own CRT heap (C088)

Call counts, one run, `X2_REACHED`:

    bootstrapMemoryPoolInitialization  x1     igArenaSystemMalloc  x1
    igMemoryPool::operator_new         x1     arenaAllocate        x2
    igArenaMemoryPool ctor             x1     addMemoryPool        NEVER
    igArenaMemoryPool::bootstrapInit   x1     setPreSize           x1
    FUN_1006aa50 (CRT heap grow)       x28    FUN_10066730         x27

Every engine-level function runs once or twice. A **single** `igArenaSystem
Malloc` produced ~527 MB, because the CRT heap beneath it grows and grows
without ever satisfying the request. `FUN_1006aa50` and `FUN_1006ae10` are
libIGCore's only two VirtualAlloc call sites, and the latter is never entered.

## Next — ANSWERED, see Resolution at the end of this file

All three were done and the issue is fixed; kept for the trail, not as work.
(1) was the right thread: the predicate is the `MEM_FREE` test in the
`VirtualQuery` scan, and it was our answer that was wrong, not the translation.
(2) was resolved by keying the reached set on (entry point, module base) —
both counts are libIGCore's, libIGSg was never entered. (3) was a red herring:
the reserve/commit size difference is incidental.

1. Read `FUN_1006aa50` and find its termination condition — what it checks after
   a successful VirtualAlloc to decide the request is still unsatisfied. That
   predicate is where the translation or the host answer diverges.
2. Resolve the ambiguity C088 names: `FUN_1006a500` (x52) and `FUN_10066730`
   (x27) exist at the same linked address in **both** libIGCore and libIGSg, and
   the reached set keys on the linked address, so those counts may be sums. Only
   `FUN_1006aa50` x28 is unambiguous. Per-module counts would settle it.
3. Compare what the host returns for the reserve/commit pair against what Wine
   returns: stock reserves slightly MORE than it commits (0x1390000 vs
   0x1388000), ours reserves and commits the same size. If the CRT expects the
   commit to be a sub-range of a larger reservation, an exact-fit reservation
   could be what it rejects.

## Reproduce

    cmake -S . -B scratch/build-reached -DCMAKE_BUILD_TYPE=RelWithDebInfo \
          -DX2_NATIVE_REACHED=ON
    cmake --build scratch/build-reached --target x2native -j4
    set -a; . ./.env; set +a
    X2_REACHED_SELFTEST=1 \
    X2_REACHED=0x00403420,0x1003d900,0x1003ab00,0x1005bdc0,0x100548a0,0x1005d8a0,0x1003b540,0x1003c1f0 \
    X2_PEEK='XMen2+0x27f708:4,libIGCore+0x15f3fc:1,libIGCore+0x15f3f0:4' \
    scratch/build-reached/x2native --no-window --run

### Resolution (2026-08-06)
ROOT CAUSE: imp_KERNEL32_VirtualQuery never consulted the reservation table that imp_KERNEL32_VirtualAlloc maintains (g_reserved[], same file, ~100 lines apart). It reported an address as MEM_FREE unless it fell in a mapped module or the guest heap -- so memory the guest had just reserved through VirtualAlloc read back as free. libIGCore's statically-linked MSVC CRT grows its heap by scanning with VirtualQuery for a free region and reserving it (FUN_1006aa50), so it was told its own reservations were still free and the scan never finished: 28 grows, ~527 MB, stopping only when the budget refused. That failure returned -1 to igArenaSystemMalloc, which took its OOM path into trimAll, which dereferences a pool list bootstrap had not built yet -- the SIGSEGV at 0x4. FIX: VirtualQuery now answers from the same table, reporting a reservation as MEM_COMMIT (honest: the reservation is mapped PROT_READ|PROT_WRITE), and a FREE span now stops at the next reservation as well as the next module, so RegionSize no longer runs through memory the guest owns. MEASURED: VirtualAlloc calls 67 -> 2, one 19.6 MB reserve plus its commit, at 0x00a80000 (low, like the stock build's 0x2410000). initBootstrap is now REACHED and trimAll is NEVER called. The run advances past the whole memory-pool bootstrap and now stops in igArenaMemoryPool::consolidate (0x10057dc0) on a NULL dereference -- filed separately. Same defect class as commit ee9707a and Xbox C070/C071: the allocator and the query must describe the same address space.
