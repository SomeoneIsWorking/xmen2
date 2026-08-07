---
id: C146
kind: claim
status: holds
created: 2026-08-07
tags: pc,native,kernel32,memory,rc-exe
---

## Claim

The arena fault at 0x068effcc was VirtualQuery and VirtualFree disagreeing about what was committed, not the arena's arithmetic: VirtualFree(MEM_DECOMMIT) mprotected pages PROT_NONE while VirtualQuery kept reporting the whole reservation as MEM_COMMIT, and VirtualAlloc(MEM_COMMIT) over an existing reservation returned success without restoring access, making a decommit permanent.

## Evidence

X2_VERBOSE=1: 'VirtualQuery(0x068ec000) -> COMMIT', then 'VirtualFree(0x068ec000, 16384, type 0x4000)', then the same query answering COMMIT again -- and the fault address 0x068effcc is inside that decommitted 16 KB. The four prior claims about this allocator (C087, C088, C090, C091) were read first and none of them was this. After the two calls were made to share one record of committed ranges, the run goes past igArenaSystemMalloc entirely and reaches the intro movie, stopping on WINMM.dll!timeSetEvent.

## What would falsify it

a VirtualQuery answering MEM_COMMIT for an address inside a range VirtualFree decommitted, or another fault inside igArenaSystemMalloc on an address that was never decommitted
