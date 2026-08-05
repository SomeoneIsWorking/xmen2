---
id: I015
kind: instrument
status: trusted
created: 2026-08-05
---

## Instrument

recomp --isolate + generated recomp_overrides.cmake (the override wiring, xbox/overrides.json as single source)

## Validated by

Replaces the hand-wired --wrap list that instrument I014 caught failing silently. Validated on BOTH classes on 2026-08-05. NEGATIVE (the case that was broken): the wrapper on sub_00275920, whose caller shares a chunk with it, reported '0 calls' while the crash stack showed the function running. POSITIVE (after isolation): '[LISTSCAN] isolation self-test passed: 2 calls reached the wrapper on sub_00275920'. Structural check: all six overridden functions have 0 intra-chunk callers (grep -c RECOMP_DCALL against their own TU), where RtlAllocateHeap previously had 2. The lift exits non-zero if a listed override was not isolated, and CMake FATAL_ERRORs if recomp_overrides.cmake is missing, so a build with silently-absent overrides cannot be produced. The sub_00275920 wrapper is kept in overrides.json as the standing regression test: if it ever reports 0 calls on a run reaching the ARK symbol table, every override is suspect.

## Known failure modes

(none recorded yet)
