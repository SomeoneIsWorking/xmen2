---
id: 19
title: The guest frees a pointer the guest heap never allocated
status: resolved
symptom: guest_heap: free of a pointer outside the guest heap (guest pointer 0x068bf000), reached during shutdown after the DirectX dialog
tags: pc,recomp,native,memory,heap
created: 2026-08-06
updated: 2026-08-06
---

## What is happening

The run reaches shutdown and calls the CRT `free` on 0x068bf000, which the
guest heap correctly refuses -- its magic-word headers say the pointer is not
one of its own.

## The hypothesis, NOT yet measured

There appear to be two allocators where the original had a clearer story:

1. `src/native/guest_heap.c`, which serves the imported
   MSVCRT/MSVCR71 `malloc`/`free`/`realloc`/`calloc`. libIGCore does import
   all four, so those calls land here.
2. libIGCore's own statically-linked MSVC heap, which `igArenaMemoryPool`
   grows through `VirtualAlloc` (FUN_1006aa50). That code is recompiled and
   real, and its pointers are NOT guest-heap pointers.

If a block from (2) is freed through (1), this is exactly what happens. That
would be a genuine split introduced by the port: the two are the same heap
inside a real MSVCRT.dll.

## What has NOT been established

* Which allocator produced 0x068bf000. That is the first thing to measure, and
  it decides whether this is a port defect or faithful behaviour.
* Whether the original has the same split. Statically-linked CRT code in a DLL
  does keep its own heap on Windows too, so "the original had one heap" is an
  assumption, not a fact.

## Next

1. Find the allocation. `X2_ARGS` on the arena and CRT allocators, plus the
   address, should name it.
2. If it came from libIGCore's internal heap, the question becomes whether the
   imported `free` should recognise and delegate to it -- which is what a
   single MSVCRT would do -- rather than refusing.
3. Note the refusal is CORRECT as a check; the bug, if there is one, is upstream
   of it.

### Resolution (2026-08-06)
ROOT CAUSE was not the two-allocator split this issue hypothesised -- that hypothesis was WRONG, and the 'what has NOT been established' section is what stopped it being acted on. imp_KERNEL32_VirtualFree was a one-liner calling guest_free(). VirtualAlloc never allocates from the guest heap -- it mmaps -- so every VirtualFree handed guest_free a pointer it had never issued. 0x068bf000 is exactly a VirtualAlloc MEM_COMMIT address from the run's own [mem] log, which is what identified it: a malloc'd block sits inside a page, not on the boundary. No import crossing appears in the ring either, because the guest was calling VirtualFree, not free. FIX: VirtualFree now does what its counterpart does. MEM_RELEASE munmaps the whole reservation, taking the size from the reservation table because Win32 requires dwSize == 0, and refuses to unmap an address this host never reserved. MEM_DECOMMIT mprotects PROT_NONE so a use-after-decommit faults instead of silently reading stale data, leaving the reservation in place so VirtualQuery still reports the range as the guest's. VERIFIED: the free error is gone, pairs entered 4958 -> 5016, battery 33/33. Worth keeping: guest_heap's refusal is the only reason this was found rather than corrupting a free list, and the run had to get all the way to shutdown before any code released memory.
