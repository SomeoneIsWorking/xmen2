---
id: I018
kind: instrument
status: trusted
created: 2026-08-05
---

## Instrument

Guest call-site attribution on every recompiled call (RECOMP_DCALL / RECOMP_ICALL_SAFE / RECOMP_ITAIL carry the call instruction's own VA; recomp_site_str + generated recomp_enclosing_func render it as 'guest 0x… (sub_…+0xNN)')

## Validated by

XBOX_ICALL_SELFTEST=1 checks BOTH classes in the shipping binary: a VA four bytes inside a known function must resolve to that function, and a VA below every function must resolve to 0. Getting only the positive right would still pass with a lookup that returns the nearest entry unconditionally, which names a wrong function for every unmapped site. Proven on the real blocker: it named 'guest 0x002A975F (sub_002A9570+0x1EF)' where the symbolized native stack could only say 'sub_002A9570 +0x1066' -- a compiled-C offset that does not map to a guest offset. It also turned 7 scattered [ABI] lines into one nested caller chain that pointed straight at the innermost violator.

## Known failure modes

(none recorded yet)
