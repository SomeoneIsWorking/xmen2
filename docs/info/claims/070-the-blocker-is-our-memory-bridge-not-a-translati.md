---
id: C070
kind: claim
status: holds
created: 2026-08-05
tags: xbox
---

## Claim

The blocker is OUR memory bridge, not a translation defect: the title reserves virtual address space at an address it PICKED from an NtQueryVirtualMemory walk, and bridge_NtAllocateVirtualMemory ignores the requested base -- it calls xbox_HeapAlloc and returns a bump-allocator address. The caller (sub_0027BEF0, at loc_0027C12E) compares the returned base against the requested one and fails on any mismatch, so the engine's arena is never created. Two bridges disagree: NtQueryVirtualMemory advertises everything at or above 64 MB as MEM_FREE, while the guest map is only 64 MB, so no reservation there could ever be backed.

## Evidence

The run prints, from the bridge itself: '[KERNEL] PLACED reserve at 0x04000000 size=8323072: returning 0x02900000 instead'. A gdb trace of bridge_NtQueryVirtualMemory shows the five-step walk that chose that address: 0x00000000 (free, 64 KB, too small) -> 0x00010000 (committed) -> 0x00780000 (committed) -> 0x00F80000 (committed) -> 0x04000000 (advertised free, 0xFBFFF000 bytes). XBOX_ICALL_WATCH over the caller chain shows the -1 propagating: sub_0027A9B0 -> al=0, sub_0026E920 -> 0xFFFFFFFF, sub_0026E740 -> 0xFFFFFFFF, sub_0026C410 -> 0xFFFFFFFF.

## What would falsify it

if honouring the placed base in bridge_NtAllocateVirtualMemory does NOT get sub_0026E740 past its -1 exit, the address comparison is not what the caller is failing on
