---
id: 93
title: Clang x87 host return capture records zero
status: resolved
symptom: test_x87host reports ST0 return was not captured: depth=1 value=0.000000 when compiled with Clang
tags: pc,native,x87,clang,compiler,inline-assembly,tests
created: 2026-08-21
updated: 2026-08-21
---

## Root cause

`x87_host_end` initialized each `long double` output slot to `0.0L` in C before `fstpt`. Clang kept the x87 zero used by that initialization live across the inline assembly, placing it above the host call return value; `fstpt` therefore captured the compiler temporary rather than ST0. The disassembly showed `fldz` immediately before the drain loop.

## Resolution and verification

Drain ST0 directly into the uninitialized output slot, then increment the count. `fstpt` writes the ten bytes later read by `fldt`, so no C initialization is required. The Clang disassembly no longer inserts an x87 value before the drain, `test_x87host` passes, and the complete Clang CTest suite is rerun as the landing gate.
