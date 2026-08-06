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

## Why the NULL is reached (C090, 2026-08-06)

`consolidate` pulls a freed chunk off a fastbin, walks to the chunk after it,
and at `0x10057efb` compares that against `ms->top`. If they are equal it takes
a merge path that never unlinks. They are not equal, so it unlinks the top
chunk and reads its `fd` -- which is zero.

    esi        = 0x00a80004   freed chunk, head 0x203, decoded span 24 bytes
    walk lands = 0x00a8001c
    ms->top    = 0x00a8002c   (malloc state 0x71002328, field +0x2c)
                              16 bytes higher

Arena bytes at the failure say the walk is right and `ms->top` is not:

    0x00a80004  0x00000203   freed chunk in a fastbin
    0x00a8001c  0x80000180   head; with its +8 word (0x94000027) decodes to
                             size 0x138000C -- the full 19.5 MB arena, so this
                             IS the top chunk
    0x00a80024  0x94000027   high size bits for the chunk at 0x00a8001c
    0x00a80028  0x00000000   <-- read as fd, hence the NULL
    0x00a8002c  0x00000000   where ms->top points: head is ZERO, not a chunk
    0x00a80030  0x00000040
    0x00a80034  0x94000027   the same high size word again, 16 bytes on

`igArenaInitState` sets `ms->top` from the aligned arena base returned by the
pool's virtual at `[vtable+0x60]`, then writes the top header at `[ms->top]`
and ORs bit 0 into it. The header at `ms->top` is zero, so either that write
did not land where `ms->top` says, or `ms->top` was moved afterwards by a carve
that wrote its new header 16 bytes low.

## What this does NOT establish

Which side is wrong. This is a hand-trace of bit-packed header arithmetic
against a memory dump, not a comparison with the original. If the shipped DLL
produces the same `ms->top` and the same 0x203 header, the defect is in neither
and `consolidate` is being reached in a state the stock build never gets into.

## Correction and current standing (2026-08-06, later)

Two things changed after the trace build, and one of them retracts a guess.

**The boundary ring was misattributing every per-body entry (I026).** Its
enter/exit hook records the LINKED entry point, while host-side crossings record
a MAPPED address, and the dump decoded both as mapped -- so libIGCore functions
came out labelled `libIGUtils.dll!0x1003c420`, and others as "no registered
module". Fixed by recording the module base alongside the ep. After the fix
every line resolves to libIGCore with a plausible name and nothing is
unattributed. Anything read off this ring before the fix is not evidence.

**Retracted:** the guess that `[ESP+0x10]` in `igArena_malloc` (the encoded
chunk size) and `EBX` (the amount `ms->top` advances) disagreed within one
carve. The corrected ring shows `consolidate` runs EARLY in `igArena_malloc`,
before the carve code at 0x100572d3, and `igArena_malloc` has been entered 3
times. So the malformed chunk was created by an EARLIER call, and this call's
`[ESP+0x10]` is 0x108 (264) -- unrelated to the 24-byte chunk. The two-quantity
story was a coincidence of arithmetic, not a measurement.

`igArena_malloc`'s live frame at the fault, for the record (entered with
esp 0x700ffe60; prologue `SUB ESP,0x20` plus four pushes):

    esp+0x10  0x00000108   this call's request size (264)
    esp+0x18  0x71002328   the malloc state
    esp+0x1c  0x00a8001c   the real top chunk
    esp+0x2c  0x1005d7e2   return address
    esp+0x30  0x1005d5a4

What still stands from C090: `ms->top` is 0x00a8002c while the arena's actual
top chunk header is at 0x00a8001c, and the chunk walk from 0x00a80004 (span 24)
lands on 0x00a8001c, so the walk and the heap agree and `ms->top` does not.

## The whole allocator history, measured (C091, I027)

The native build had no argument watch, so "how often" was answerable and "with
what" was not. Added one (`X2_ARGS`, trace builds). The complete history before
the crash is four calls:

    igArena_malloc(0x10)          -> 0x00a80008
    igArena_free (0x00a80008)                     <- so the fastbin entry is legitimate
    igArena_malloc(0x0c)          -> 0x00a80028
    igArena_malloc(0x100)         -> consolidate -> SIGSEGV

That yields one statement needing **no** header decoding: allocation #2 has a
12-byte payload at 0x00a80028, so it occupies 0x00a80028..0x00a80034, and
`ms->top` is 0x00a8002c -- **four bytes inside a live allocation**. A top chunk
overlapping a live block is corruption whatever the encoding is, and it is why
`consolidate`'s walk never matches `ms->top`.

Also measured, decode-free: payload #1 is 0x00a80008 and payload #2 is
0x00a80028, so a 16-byte request consumed 32 bytes of arena.

My own header-span arithmetic (which said chunk #1 spans 24) disagrees with that
32, so **the decode formula I derived by reading `consolidate` is itself
suspect** and nothing above rests on it.

## Next

`tests/difftest.c` against the shipped `libIGCore.dll`, unchanged as the
priority and now with a concrete script to reproduce: malloc(0x10), free it,
malloc(0x0c), malloc(0x100). Comparing the arena bytes and `ms->top` after that
sequence against the original decides the encoding, the 32-vs-24 question and
C091's falsifier in one run.

## Earlier next (superseded)

Unchanged in priority, and the retraction is the argument for it: hand-tracing
this allocator has now produced one correct localisation and one wrong story.
`tests/difftest.c` against the shipped `libIGCore.dll` is the step that does not
depend on my reading. Failing that, catch the allocation that CREATES the
24-byte chunk -- it is an earlier `igArena_malloc` call, and the reached set can
count calls but not record arguments, so this needs an argument watch on
`igArena_malloc` (the hosted build has one, `X2_WATCH`; the native build does
not).

## Earlier next (superseded)

1. **`tests/difftest.c` against the shipped `libIGCore.dll`** for
   `igArenaInitState` and the malloc path. It already does forced relocation and
   memory-write comparison and its negative controls fire (I006, C016); it is
   currently wired for libIGDisplay only. This decides translation-vs-real
   directly and is the only step here that does not rest on my reading of the
   header encoding.
2. The allocator's own `igArenaDoCheckMallocState` would audit these invariants,
   but `bootstrapInit` hardcodes the pool's debug level to 0 at `pool+0xb8`
   (`igArenaMemoryPool::setHeapIntegrityCheckLevel` @0x10018c90 is the setter,
   and `configureFromInfo` is the path that would set it from config). Enabling
   it needs a host-side diagnostic call, which is worth doing only if (1) is
   inconclusive.

## Earlier next (superseded)

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
