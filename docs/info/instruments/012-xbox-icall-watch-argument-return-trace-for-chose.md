---
id: I012
kind: instrument
status: trusted
created: 2026-08-05
---

## Instrument

XBOX_ICALL_WATCH -- argument/return trace for chosen indirect-call targets (xbox/src/recomp_manual.c)

## Validated by

XBOX_ICALL_WATCH_SELFTEST=1 drives both hooks in the shipping binary: the watched target counts 1, an unwatched target counts 0, depth returns to 0, and a second watched address that is never driven prints 'NEVER CALLED'. Both the positive and the negative are therefore proven to fire. It also replaces gdb line breakpoints, which were MEASURED to lie on this -O2 build: a breakpoint on the line after an icall reported the CALLER's register, and one on a call line fired three times for a single execution.

## Known failure modes

(none recorded yet)
