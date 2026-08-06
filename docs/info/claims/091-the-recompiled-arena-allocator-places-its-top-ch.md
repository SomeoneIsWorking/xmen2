---
id: C091
kind: claim
status: holds
created: 2026-08-06
tags: pc,recomp,native,memory,rc-exe
reconfirmed: 2026-08-06
---

## Claim

The recompiled arena allocator places its top chunk INSIDE a live allocation

## Evidence

Measured with the native argument watch and X2_PEEK, no header decoding involved. The complete allocator history before the crash is four calls: igArena_malloc(0x10) returned 0x00a80008; igArena_free(0x00a80008); igArena_malloc(0x0c) returned 0x00a80028; igArena_malloc(0x100) entered consolidate and faulted. So allocation #2 has a 12-byte payload at 0x00a80028, occupying 0x00a80028 through 0x00a80034. The malloc state's top pointer (state 0x71002328, field +0x2c) reads 0x00a8002c -- four bytes INSIDE that live allocation. A top chunk overlapping a live block is corruption whatever the header encoding is, and it is why consolidate's chunk walk (which lands on 0x00a8001c) never matches ms->top and so unlinks the top chunk instead of merging with it. Also measured: the gap between payload #1 (0x00a80008) and payload #2 (0x00a80028) is 32 bytes for a 16-byte request.

## What would falsify it

This assumes the second argument word is not part of the request -- i.e. that igArena_malloc's size argument is the first stack word (0x10, 0x0c, 0x100), which is consistent across three calls and with the freed pointer matching malloc #1's return, but is not proven. If the allocator's payload for request 0x0c is smaller than 12 bytes because the second word carries an alignment or flags that shrink it, the overlap disappears. Running the same sequence against the shipped libIGCore.dll (difftest) settles both this and the header encoding.

## Re-confirmed 2026-08-06

Sharpened and strengthened by a time series of the arena across every allocator call (X2_PEEK now fires on each X2_ARGS-watched call, not only at the fault). After malloc(0x10): chunk1 head 0x203 at 0x00a80004, top header at 0x00a8001c, ms->top = 0x00a8001c -- fully self-consistent, span 24, and this VINDICATES the header-span decode I had flagged as suspect. After malloc(0x0c): chunk2's header sits at 0x00a8001c with head 0x80000181 (sign bit set = the extension form, 12-byte header), the call returns payload 0x00a80028 = header + 12 which matches that form, but ms->top advances only to 0x00a8002c = header + 16, as though the header were 4 bytes. So the 12-byte payload spans 0x00a80028..0x00a80034 and overlaps the top chunk at 0x00a8002c by 8 bytes. Confirmed downstream: at the next call [0x00a8002c] has been overwritten with 0, i.e. the caller's writes destroyed the top header, which is why consolidate later reads garbage. This is SELF-CONTRADICTORY rather than merely different from the original -- no allocator returns a block overlapping its own top chunk -- so it is a defect regardless of what the shipped DLL does. Also measured: [pool+0xb4] is 0 across all three calls, so the branch selecting the extension form (>>1 >= 0x20) should not have been taken for a 12-byte request.
