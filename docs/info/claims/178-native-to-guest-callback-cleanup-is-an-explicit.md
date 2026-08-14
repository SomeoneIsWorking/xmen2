---
id: C178
kind: claim
status: holds
created: 2026-08-14
tags: pc,native,abi,callback
---

## Claim

Native-to-guest callback cleanup is an explicit fatal contract, and the full game loop satisfies it

## Evidence

src/native/x86rt_native.c x86_guest_call_args; src/native/x2native.c runtime-module RET 8 probe; deliberate zero-byte mutation abort; tools/smoke_loop.sh passed 4200 frames with 0 contract violations in 1460 log lines (issue #67)

## What would falsify it

any native-to-guest call returns at an ESP other than caller ESP plus its declared callee-cleaned byte count, or a callback site is found whose declared cleanup disagrees with the binary callee RET
