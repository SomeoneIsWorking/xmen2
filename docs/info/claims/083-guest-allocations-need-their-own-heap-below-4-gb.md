---
id: C083
kind: claim
status: holds
created: 2026-08-05
tags: pc,jit,native,heap,pointers
---

## Claim

Guest allocations need their own heap below 4 GB; the host allocator cannot serve them on x86-64.

## Evidence

Measured: host malloc returned 0x55b49f308020 during libIGCore's DllMain. A guest pointer is 32 bits, so that address cannot be stored, and casting it would produce a pointer that looks valid and is not. src/native/guest_heap.c serves guest allocations from a 256 MB arena at 0x40000000; 6 battery checks cover fit-in-32-bits, non-overlap, full reclaim on free, coalescing and realloc-preserves-contents.

## What would falsify it

if the game's own allocators (`igMemoryPool` and friends) serve every allocation
once engine initialization runs, this heap only backs CRT-level allocation and
its size and speed assumptions should be re-measured rather than inherited
