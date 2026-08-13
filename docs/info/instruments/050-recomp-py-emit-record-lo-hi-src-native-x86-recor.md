---
id: I050
kind: instrument
status: trusted
created: 2026-08-13
---

## Instrument

recomp.py emit --record LO-HI + src/native/x86_record.c: instruction-level region recording

## Validated by

Shown BOTH answers: the emitter refuses a range matching no instruction (rather than emitting a build that records nothing and looks identical to one with no recording); the runtime distinguishes NOT COMPILED IN / compiled in but NEVER EXECUTED / captured N entries, and the ranges register themselves from a constructor so the first two cannot be confused; the ctest 'recomp' asserts X86_RECORD is present for the addresses asked for and ABSENT for the ones next to them, and that no ranges means no instrumentation at all. First real use captured 123 entries over 3 passes of XMen2.exe 0x0045d4c2-0x0045d55b and the three passes take an identical path -- which is itself a check, since a recorder that dropped or reordered entries would not repeat.

## Known failure modes

(none recorded yet)
