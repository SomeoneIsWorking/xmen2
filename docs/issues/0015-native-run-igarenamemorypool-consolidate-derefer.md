---
id: 15
title: Native run: igArenaMemoryPool::consolidate dereferences NULL
status: open
symptom: SIGSEGV at (nil) inside Gap::Core::igArenaMemoryPool::consolidate (libIGCore 0x10057dc0), reached from igArenaMemoryPool::malloc -> memoryOperation. Occurs AFTER the memory-pool bootstrap now completes (issue #14 fixed)
tags: pc,recomp,native,memory,engine-init,rc-exe
created: 2026-08-06
updated: 2026-08-06
---

## Where it stops

This is the frontier that issue #14's fix exposed. The memory-pool bootstrap now
completes: `igMemoryPool::initBootstrap` runs, `trimAll` is never called, and the
arena pool services real malloc/free traffic with statistics
(`updateLocalStatistics` / `updateUserStatistics` in the boundary ring).

    addr2line ->  fn_libIGCore_10057dc0  =  Gap::Core::igArenaMemoryPool::consolidate

The last crossings before the fault are a repeating
malloc -> memoryOperation -> updateUserStatistics -> updateLocalStatistics cycle,
then `consolidate`.

## Not yet established

Whether this is a translation defect in `consolidate` (it walks a free list, so a
mistranslated pointer update would show up exactly like this), or a real NULL the
engine would also see because something earlier handed it a pool in a state the
stock build never produces. The stock build under Wine is the control and has not
been compared here.

## Localised to one instruction (2026-08-06)

Exactly `0x10057f27 MOV EDX,dword ptr [EDI]` with `EDI == 0`, which is the
`unlink` of a free chunk in Alchemy's bit-packed dlmalloc variant:

    10057f20  ADD EAX,EDX          ; EDX = 0xc, so EAX = chunk + 12 = &chunk->fd
    10057f22  MOV EDI,[EAX]        ; EDI = fd   -- loads 0
    10057f24  MOV EAX,[EAX+4]      ; EAX = bk
    10057f27  MOV EDX,[EDI]        ; <-- FAULTS, fd is NULL

So a chunk sitting in a free bin has a NULL forward pointer.

Registers at the fault (I024): `eax 00000000  ecx 00000028  edx 0000000c
ebx 00000000  esp 700ffe04  ebp 71002328  esi 00a80004  edi 00000000`.
`esi` is the arena base + 4, so this is the very first chunk of a fresh arena,
and `ebp` is the malloc state.

The line was obtained with `-DX2_NATIVE_O0=libIGCore_003.c` (I025) and the -O2
build independently reported the same line, so -O2 attribution is trustworthy
for this class of fault.

Arena bytes at the failure (`X2_PEEK`), arena base 0x00a80000:

    +0x00 00000000   +0x04 00000203   +0x08 00000000   +0x0c 00000000
    +0x10 00000000   +0x14 00000000   +0x18 00000000   +0x1c 80000180
    +0x20 00000000   +0x24 94000027   +0x28 00000000   +0x2c 00000000

Almost entirely zero, which is what a freshly mapped arena is.

## Call order and counts

    #1346  igArenaMemoryPool::consolidate        x2
    #1347  igArenaMemoryPool::igArenaInitState   x1
           igArenaDoCheckInUseChunk              NEVER
           igArenaDoCheckMallocState             NEVER

`consolidate` is entered BEFORE `igArenaInitState` because its own first branch
does that: `MOV EAX,[ECX]` on the malloc state, and if zero it tail-calls
`igArenaInitState` and returns. That is the ordinary "consolidate on an
uninitialised state initialises it" path, so the ordering is not itself
suspicious. The SECOND call is the one that faults.

The two debug validators (`igArenaDoCheckInUseChunk`,
`igArenaDoCheckMallocState`) are never entered because they are gated on
`[pool+0xb8] >= 1` and `>= 2` -- the pool's debug level. Raising it is a way to
make the allocator check its own invariants and is worth trying before reading
more disassembly.

## Next

1. Set the arena pool's debug level (`pool+0xb8`) so `igArenaDoCheckMallocState`
   runs, and let the allocator's own consistency checks say what is wrong.
2. This allocator is a bit-packed dlmalloc variant with heavy SHR/SHL/XOR/AND
   header arithmetic -- exactly the shape a translation defect would hide in.
   `tests/difftest.c` already verifies recompiled bodies against the ORIGINAL
   DLL by forced relocation and memory-write comparison; pointing it at
   `igArenaInitState` and `consolidate` would settle translation-vs-real
   directly, instead of by reading more x86.
3. Not yet done: compare against the stock build under Wine.

## Original next (superseded)

1. `X2_REACHED` the free-list path around `consolidate` for call counts -- a
   consolidate that runs once and dies differs from one looping over a corrupt list.
2. `X2_PEEK` the arena descriptor at the fault to see which pointer is NULL.
3. Compare against the stock build under Wine before assuming the translation is
   at fault.
