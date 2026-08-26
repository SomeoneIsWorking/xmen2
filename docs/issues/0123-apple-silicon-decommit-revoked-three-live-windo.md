---
id: 123
title: Apple Silicon decommit revoked three live Windows pages beside it
status: resolved
symptom: Starting New Game crashes after i105.sfd with SIGBUS at 0x068b9004 in igArenaMemoryPool::getHighestAddress
tags: pc,native,macos,arm64,memory,kernel32
created: 2026-08-26
updated: 2026-08-26
---

## Root cause

Win32's allocator commits and decommits 4 KiB pages. Apple Silicon applies
`mprotect` at 16 KiB granularity. `guest_memory.c` had made its logical page
table 16 KiB too, so `VirtualFree(0x068bb000, 0x5000, MEM_DECOMMIT)` rounded
down to `0x068b8000` and removed access from the still-live allocator header at
`0x068b9004`. The next `igArenaMemoryPool::getHighestAddress` read faulted.

The verbose memory trace supplies the complete denominator: `0x06800000` was
reserved through `0x068bffff`; the guest separately committed `0x068b9000`,
`0x068ba000`, and `0x068bb000`; only the range beginning at `0x068bb000` was
later decommitted. The fault is therefore host protection widening, not an
unaligned x86 load or guest allocator arithmetic.

## Fix and verification

`guest_memory.c` now records mapping and protection at the Windows 4 KiB page
size. On Apple Silicon it derives each 16 KiB host protection from the union of
the four logical pages: the group stays accessible while any member is
accessible. `VirtualQuery` and `guest_memory_is_readable` still see exact 4 KiB
state. This is the closest protection the hardware can express; direct host
access could physically reach a decommitted quarter when a committed sibling
keeps the group open, so host boundaries must continue validating guest spans.

The native battery maps two 4 KiB siblings in one Apple granule, decommits one,
checks its logical state, and reads the committed sibling. It passes 93/93.
Replaying Return through the retail boot clears all six intro movies, passes
the old post-`i105.sfd` fault, enters the world renderer and sustains tens of
thousands of geometry draws per heartbeat with the shadow pass active.
