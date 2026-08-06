---
id: C089
kind: claim
status: holds
created: 2026-08-06
tags: pc,recomp,native,memory,engine-init,rc-exe
---

## Claim

The recompiled exe now completes the engine's memory-pool bootstrap; the cause was VirtualQuery and VirtualAlloc describing different address spaces

## Evidence

imp_KERNEL32_VirtualQuery never consulted g_reserved[], the table imp_KERNEL32_VirtualAlloc maintains in the same file, so an address the guest had just reserved read back as MEM_FREE. libIGCore's statically-linked MSVC CRT grows its heap by scanning with VirtualQuery for a free region and reserving it (FUN_1006aa50), so the scan never terminated. Fixed by answering VirtualQuery from the same table (a reservation reports MEM_COMMIT, which is honest -- it is mapped PROT_READ|PROT_WRITE) and by clamping a FREE span at the next reservation as well as the next module. MEASURED before -> after in the same build: VirtualAlloc calls 67 -> 2; reservations ~527 MB -> one 19.6 MB reserve plus its commit at 0x00a80000, which is low like the stock build's 0x2410000; igMemoryPool::initBootstrap NEVER -> REACHED; igMemoryPool::trimAll REACHED -> NEVER; distinct (entry point, module) pairs entered 1362 -> 1372. The 33-check native battery still passes 0 failures and ctest is 3/3.

## What would falsify it

Reporting a reservation as MEM_COMMIT rather than MEM_RESERVE is a deliberate simplification: the host maps a MEM_RESERVE request readable and writable immediately, so it cannot distinguish the two. A guest that reserves without committing and expects a fault on access would not get one -- if any caller is observed depending on RESERVE vs COMMIT, this answer is wrong and the host must track commit separately.
