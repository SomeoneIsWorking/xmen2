---
id: I030
kind: instrument
status: trusted
created: 2026-08-06
---

## Instrument

tests/test_vformat.c: known-answer tests for the guest printf walker

## Validated by

11 checks covering %d/%x/%s/%c/%f/%%, star width, MSVC's I64 spelling, a NULL %s, and truncation semantics. The case that earns its keep is 'int, double, int stay in step': a double occupies TWO argument slots on the x86-32 stack, and a walker that advanced 4 bytes per argument would print the first two correctly and desynchronise everything after -- which is the shape this parser fails in, and it would surface as a wrong filename three subsystems away rather than as a format bug. Guest memory is host memory under the identity mapping, so a plain array stands in for the guest stack and the walker is fed exactly what a real call gives it. The runtime symbols crt.c references but the walker never reaches are stubbed to ABORT rather than return a value, so a future dependency on one fails loudly instead of passing on a fabricated answer.

## Known failure modes

(none recorded yet)
