---
id: I005
kind: instrument
status: trusted
created: 2026-08-04
---

## Instrument

tools/recomp.py -- x86-32 to C translator with fail-loud coverage reporting

## Validated by

Caught THREE of its own bugs by refusing to emit rather than guessing, each visible as a named blocker in the report: hex scale factors (EDX*0x1) misparsed, IAT-resolved calls shadowed by an earlier JMP handler, and untyped memory operands (MOV AL,[addr]) defaulting to 4 bytes wide -- that last one would have read 3 bytes too many, silently. Coverage went 33.6% -> 78.5% -> 96.0% as each was fixed, so the blocker ranking demonstrably points at real work. The C compiler then caught a fourth: JMP to another function's entry is a TAIL CALL, not a goto, which surfaced as 'label used but not defined' rather than as wrong code. Refuses to run at all if the .iat file is missing, because without it every import call would look unresolvable and the coverage number would be quietly meaningless.

## Known failure modes

(none recorded yet)
