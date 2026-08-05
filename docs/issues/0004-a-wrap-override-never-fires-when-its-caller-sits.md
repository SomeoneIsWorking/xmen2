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

### Note (2026-08-05)
FIXED 2026-08-05, by construction rather than by care.

`recomp --isolate <overrides.json>` emits every overridden function into its own translation unit (`recomp_iso_<ADDR>.c`), so every call site is cross-object and --wrap always binds. `xbox/overrides.json` is the single source of truth: tools/xbox_relift.sh feeds it to the recompiler AND generates the --wrap link flags from it into `recomp_overrides.cmake`, so the isolation list and the wrap list cannot drift. The lift EXITS NON-ZERO if any listed override did not get an isolated file, and CMake FATAL_ERRORs if the generated file is absent -- building a binary whose overrides are all silently missing is no longer possible.

Verified both classes:
- BEFORE: the wrapper on sub_00275920 reported 0 calls on a run whose crash stack contained sub_00275920+0x318.
- AFTER: `[LISTSCAN] isolation self-test passed: 2 calls reached the wrapper on sub_00275920`. That wrapper stays in overrides.json permanently as the regression test -- it is the case that was broken.
- Structurally: all six overridden functions now have 0 intra-chunk callers (was 2 for RtlAllocateHeap).

The two RtlAllocateHeap sites that bypassed the native heap are now wrapped. NHEAP still reports 17 allocs on this run, unchanged, which suggests those two sites are not reached on the boot path -- consistent with the earlier note that the hole's size was unknown; it is now closed either way.
