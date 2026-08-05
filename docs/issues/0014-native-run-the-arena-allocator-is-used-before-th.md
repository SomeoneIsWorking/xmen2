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

Determine whether bootstrapMemoryPoolInitialization is reached at all, with a
traced build (-DX2_NATIVE_TRACE=ON, grep the ring for 0x1003d900). If it never
runs, the question is what should have called it -- an engine entry point the
exe invokes, which would mean the startup path diverges earlier than this.
