---
id: 79
title: The intro FMV spins forever in libCriMovie while the main thread waits on it
status: open
symptom: The window stops repainting and the desktop marks it (Not Responding). The process burns ~98% of a core, the heartbeat says 'the guest executed NOTHING in the last 5.0s (crossings unchanged)', MAIN is in a condition-variable wait and one worker thread is running guest code that never returns.
tags: pc,native,fmv,crimovie,hang
created: 2026-08-15
updated: 2026-08-21
---

## Symptom

A live driven run stopped repainting; the compositor marked the window Not
Responding. Heartbeat at the time:

    [HB]  55.0s  the guest executed NOTHING in the last 5.0s
                 (crossings unchanged at 101009286) -- it is blocked inside
                 host code or stopped, not looping.
    [HB]  MAIN tid 999: in a WAIT (condition variable) for 38.7s
    [HB]  tid 1014: running guest code for 38.7s  <- running

`ps` showed 98.2% CPU, so it is a spin, not a deadlock.

## Where

gdb on the live process (this is a native ELF binary -- gdb works here, unlike
the Wine path of issue #1). The running thread:

    fn_libCriMovie_10040000        libCriMovie_001.c:153227/153234
      <- x86_native_call_at / x86_dispatch
    fn_libCriMovie_1002a640        libCriMovie_001.c:50248
    fn_libCriMovie_1002a4c0        libCriMovie_001.c:48896

Successive samples showed different line numbers inside 0x10040000, so it is
looping inside that function rather than stuck on one instruction.

All three are REAL bodies in the export, not stubs -- 0x10040000 is 591
instructions / 1677 bytes, 0x1002a640 is 256 instructions, 0x1002a4c0 is 60.
So this is not the truncated-function class of issue #76 / commit 360fa6a.

## Reading

The FMV decoder thread never finishes its work and the main thread waits on it
for ever. This is very likely the same defect behind the long-standing 'FMVs
glitchy' report, seen at its extreme.

## Not yet done

Which loop inside 0x10040000, and what condition it is waiting on -- a file
read that never completes, a ring buffer that never drains, or a timing source
that never advances. The Wine control plays the FMVs, so it is a port defect
and the control can answer what the loop expects to see.

### Note (2026-08-21)
2026-08-21 current-build reproduction, windowless: live control selected New Game and confirmed Normal difficulty from screenshot-verified menus. The story FMV rendered, then frame count stopped at 18,622 for 20 consecutive one-second status samples while guest time and the loopback control server continued advancing/responding. Ten Escape requests raised the request count but the accepted-key count stayed at 8, proving the movie path was not polling DirectInput; no scheduled-input miss is involved. The run was stopped by its exact session. This independent hang prevents a full menu-route replay of resolved issue #81, but does not overlap its deterministic tail-depth contract.
