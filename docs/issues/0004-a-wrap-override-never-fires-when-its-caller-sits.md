---
id: 4
title: A --wrap override never fires when its caller sits in the same generated chunk
status: resolved
symptom: A native override wired with -Wl,--wrap=sub_XXXXXXXX reports zero calls while a crash stack plainly shows the function running; the instrument reads as 'this code path is never taken'
tags: xbox,recomp,overrides,linker,instruments
created: 2026-08-05
updated: 2026-08-05
---

## Symptom

An observer wrapper on `sub_00275920` (the list remove + tail shift) printed
`[LISTSCAN] 0 calls to the list-remove at 0x00275920` on a run whose own crash
stack contained `sub_00275920 +0x318`. The function ran; the wrapper did not.

## Cause

`-Wl,--wrap=X` rewrites *undefined references* to `X` at link time. A call
inside the same object file that DEFINES `X` is not an undefined reference --
the compiler bound it while emitting the translation unit -- so `--wrap` never
sees it.

The generated code is emitted in `--split 1000` chunks, so whether an override
fires depends entirely on which chunk the recompiler happened to put the caller
in. `sub_00275920` and its caller `sub_00289F50` both landed in
`recomp_0014.c`, so every call between them bypassed the wrapper.

## Consequence beyond the observer (unresolved)

The same mechanism carries the native heap override. Counting direct call sites
in the generated tree:

| function | defined in | intra-chunk call sites (bypass) | total direct |
|---|---|---|---|
| `sub_002241E1` RtlAllocateHeap | recomp_0011.c | **2** | 11 |
| `sub_00222433` RtlFreeHeap | recomp_0011.c | 0 | 9 |
| `sub_00224B50` RtlReAllocateHeap | recomp_0011.c | 0 | 1 |

So two RtlAllocateHeap call sites reach the RECOMPILED heap even with
`XBOX_NATIVE_HEAP=1`, and a pointer from one of them freed through the wrapped
`RtlFreeHeap` crosses allocators. **Whether those two sites are REACHED at
runtime is not established** -- this is a call-site count, not a fire counter --
so the size of the hole is unknown, but the A/B toggle is not the clean switch
it is documented to be. See the note added to xb-nheap.

## Fix

For the observer: move it to a boundary that really crosses objects. The CRT
`memcpy` at `sub_003D5890` lives in `recomp_0021.c` and its callers do not, so
wrapping it works, and it is a better observation point anyway.

For the general case there are two honest options, neither applied yet:

1. Emit each overridden function into its own translation unit (a recompiler
   `--isolate <addrs>` option), so every call site is cross-object by
   construction. This makes `--wrap` sound rather than lucky.
2. Route overrides through the dispatch table instead of the linker.

## Rule this leaves behind

An override wired with `--wrap` MUST prove it fires -- a fire counter in the
run report, not an assumption. Before trusting one, check that the callers are
in a different chunk than the callee:

    grep -ln '^void sub_XXXXXXXX' recomp_*.c        # the definition's chunk
    grep -c 'RECOMP_DCALL(sub_XXXXXXXX,' <that file>  # calls that bypass --wrap
