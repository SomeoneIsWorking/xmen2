---
id: 14
title: Native run: the arena allocator is used before the memory system is bootstrapped
status: open
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

## Next

Answered: it is reached (#1322). The open question is now **why the arena
allocation loop does not terminate**, and the falsifier for the current reading
has to be measured first:

1. **Run the Wine oracle and count its VirtualAlloc calls during startup.** If
   the stock build also reserves until the OS refuses, the walk is the engine's
   normal strategy, the defect is only trimAll's missing pool list, and the
   question becomes why the real game's list exists by then. If the stock build
   reserves a bounded amount, the loop is ours and the divergence is inside
   `igArenaMemoryPool::bootstrapInit` / `setPreSize` (both in the boundary ring
   at the failure).
2. The reservations come from libIGCore's own statically-linked MSVC heap
   (`FUN_1006aa50` and `FUN_1006ae10` are its only VirtualAlloc call sites), so
   what the guest asks *malloc* for is the number that matters — log the
   requested size at the CRT boundary, not just the VirtualAlloc size.
3. `igMemoryPoolContext::bootstrapMemoryPoolInitialization` loops EBX 0..0x2f
   (48 pools). Confirm whether iteration 0 is the one that never returns, or
   whether the loop advances at all, before assuming a single arena is at fault.

## Reproduce

    cmake -S . -B scratch/build-reached -DCMAKE_BUILD_TYPE=RelWithDebInfo \
          -DX2_NATIVE_REACHED=ON
    cmake --build scratch/build-reached --target x2native -j4
    set -a; . ./.env; set +a
    X2_REACHED_SELFTEST=1 \
    X2_REACHED=0x00403420,0x1003d900,0x1003ab00,0x1005bdc0,0x100548a0,0x1005d8a0,0x1003b540,0x1003c1f0 \
    X2_PEEK='XMen2+0x27f708:4,libIGCore+0x15f3fc:1,libIGCore+0x15f3f0:4' \
    scratch/build-reached/x2native --no-window --run
