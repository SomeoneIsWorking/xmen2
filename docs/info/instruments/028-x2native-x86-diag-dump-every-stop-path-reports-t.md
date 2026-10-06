---
id: I028
kind: instrument
status: trusted
created: 2026-08-06
---

## Instrument

x2native x86_diag_dump: every stop path reports the same set

## Validated by

`x86_diag_dump()` directly aggregates the current engine location, configured
guest-memory peeks, multimedia-timer report, and boundary ring before every
abort path and from the SIGSEGV handler; it does not depend on `atexit()`.
Validated by the bounded native battery: the fail-loud PUSHFD stop named the
active guest-call frame and printed the final 96 of 356 title/host crossings.

## Known failure modes

It deliberately does not print clean-exit subsystem reports; those are owned by
`x2_interrupt_reports`, so an interrupted run does not duplicate counters.
