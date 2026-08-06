---
id: C091
kind: claim
status: holds
created: 2026-08-06
tags: pc,recomp,native,memory,rc-exe
---

## Claim

The recompiled arena allocator places its top chunk INSIDE a live allocation

## Evidence

Measured with the native argument watch and X2_PEEK, no header decoding involved. The complete allocator history before the crash is four calls: igArena_malloc(0x10) returned 0x00a80008; igArena_free(0x00a80008); igArena_malloc(0x0c) returned 0x00a80028; igArena_malloc(0x100) entered consolidate and faulted. So allocation #2 has a 12-byte payload at 0x00a80028, occupying 0x00a80028 through 0x00a80034. The malloc state's top pointer (state 0x71002328, field +0x2c) reads 0x00a8002c -- four bytes INSIDE that live allocation. A top chunk overlapping a live block is corruption whatever the header encoding is, and it is why consolidate's chunk walk (which lands on 0x00a8001c) never matches ms->top and so unlinks the top chunk instead of merging with it. Also measured: the gap between payload #1 (0x00a80008) and payload #2 (0x00a80028) is 32 bytes for a 16-byte request.

## What would falsify it

This assumes the second argument word is not part of the request -- i.e. that igArena_malloc's size argument is the first stack word (0x10, 0x0c, 0x100), which is consistent across three calls and with the freed pointer matching malloc #1's return, but is not proven. If the allocator's payload for request 0x0c is smaller than 12 bytes because the second word carries an alignment or flags that shrink it, the overlap disappears. Running the same sequence against the shipped libIGCore.dll (difftest) settles both this and the header encoding.
