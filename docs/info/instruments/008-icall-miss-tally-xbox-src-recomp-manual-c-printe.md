---
id: I008
kind: instrument
status: trusted
created: 2026-08-05
---

## Instrument

ICALL miss tally (xbox/src/recomp_manual.c, printed at end of every Xbox run)

## Validated by

Run against BOTH classes. Positive: before seeding, it named 0x00225995 as unresolved on a run that otherwise printed 'main thread returned' -- the target it was blind to. Negative: after seeding, '16 indirect calls, 0 did NOT execute'. XBOX_ICALL_SELFTEST=1 feeds one unresolved and one range-skipped VA and asserts both counters move. Blind spot it states itself: DIRECT calls into stubbed addresses are not counted.

## Known failure modes

(none recorded yet)
