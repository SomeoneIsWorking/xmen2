---
id: I006
kind: instrument
status: trusted
created: 2026-08-04
---

## Instrument

tests/difftest.c -- differential test of recompiled functions against the original DLL

## Validated by

Validated against BOTH classes, and the first two attempts were caught lying. (1) It initially reported 15 FAILURES that were not recompiler bugs at all: empty virtual methods that just RET and never set a return value, where it was comparing stale EAX against a zeroed CPU -- fixed by seeding EAX to a sentinel on both sides, which additionally proves void functions do not clobber EAX. (2) The FIRST negative control did not fire: mutating AND EAX,0x600 -> 0x601 changed nothing because two later SHRs (>>2, >>6) discard bit 0 -- the mutation was provably invisible, so this was a bad control, not a blind test. Re-run with two mutations that survive to the result (AND EDX,0xf->0x7, SHL EDX,8->9) and each was detected as exactly 1 failing case. It therefore detects SEMANTIC differences, not textual ones. Refuses to report success when nothing ran: prints 'NOTHING WAS COMPARED' and exits 2, and counts/prints trials skipped because the ORIGINAL faulted on an invalid object (35,300 of 328,000).

## Known failure modes

(none recorded yet)
