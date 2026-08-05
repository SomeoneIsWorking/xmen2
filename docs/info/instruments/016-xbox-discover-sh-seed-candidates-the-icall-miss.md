---
id: I016
kind: instrument
status: trusted
created: 2026-08-05
---

## Instrument

xbox_discover.sh seed candidates (the ICALL miss tally read as 'a function the detector missed')

## Validated by

DISTRUST WITHOUT THE KIND FIELD. Verified 2026-08-05 that this instrument produced three FALSE functions: 0x003D5B44/54/88 are mid-function instructions in memcpy's unrolled copy tail, reported as unresolved indirect calls because RECOMP_ITAIL (a tail JUMP through an unenumerated switch table) logged through the same path as a failed CALL. Seeding them made the loop report 'DONE -- every indirect call resolved' while the run executed fragments with no prologue and died with a C++ this pointing into the stack (issue #6). NEGATIVE CASE NOW EXERCISED: with the kind field added, the run prints UNRESOLVED-TAIL-JUMP and the loop STOPS -- confirmed end to end, exit 1 with the reason and the real fix. A seed candidate is only trustworthy if its line says UNRESOLVED (call); a TAIL-JUMP line means a translator gap and must never be seeded.

## Known failure modes

(none recorded yet)
