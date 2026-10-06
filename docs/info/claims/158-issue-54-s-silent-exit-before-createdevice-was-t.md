---
id: C158
kind: claim
status: holds
created: 2026-08-12
tags: native
---

## Claim

Issue #54's silent exit before CreateDevice was the DirectX-check override returning no value: the game does TEST AL,AL on it and took the failure branch whenever leftover EAX had a zero low byte.

## Evidence

Read out of the exe: WinMain 0x00403420 does CALL 0x00617480; TEST AL,AL; JNZ, and on the false branch sets 0x006f3c2c and 0x006f3a2d; the display initialiser 0x005fb270 reads 0x006f3a2d and sets the quit flag 0x00a09f94; WinMain reads that at 0x00403533 and skips its entire main loop, returning 0 to the CRT which calls exit(0). The original 0x00617480 ends XOR AL,AL / MOV AL,1. Fix verified: X2_QUANTUM=0, which failed 18 of 19 runs before, passes 4 of 4 after.

## What would falsify it

another override that leaves EAX untouched would reproduce the same class of failure elsewhere; check every entry in src/native/overrides.json against its CALL SITE rather than against Ghidra's signature, which typed this one void
