---
id: C056
kind: claim
status: holds
created: 2026-08-05
tags: xbox
depends: xbox/src/recomp/gen
---

## Claim

The current blocker is a CRT heap defect with a precise reproduction: the recompiled allocator sub_002241E1 returns a block at 0x00F80358, which is inside the heap's OWN free-list array (heap+0x180..+0x580), and the HEAP_ZERO_MEMORY memset that follows destroys the list heads. A later allocation then walks a list whose Flink/Blink are .text int3 padding (0x003ECDD0/0x003ECDE0) and faults writing through 0xCCCCCCCC.

## Evidence

Watchpoint on 0x00F80358 across a full run: write #1 comes from sub_00223DBD (heap create) and correctly stores 0x00F80358 -- a self-pointing empty list head. Write #2 comes from sub_002241E1 line 51461, the flags&8 zero-fill path (rep stosd of MEM32(ebp+0x10) bytes at esi), and stores 0. At the eventual crash the free list at index 0x3B has Flink=0x003ECDD0 Blink=0x003ECDE0, both int3 padding at the end of .text, and eax = Blink-8 = 0x003ECDD8 is dereferenced. Heap state is verifiably healthy at the first allocation: handle 0x00F80000, signature 0xEEFFEEFF at heap+0x10, list 0 with Flink=Blink=0x00F80688.

## What would falsify it

if the original also returns header-overlapping memory here, the fault is upstream in how much of the heap our NtAllocateVirtualMemory reports as committed -- it currently no-ops MEM_COMMIT on an already-reserved region
