---
id: I042
kind: instrument
status: trusted
created: 2026-08-07
---

## Instrument

boundary ring caller field (src/native/x86rt_native.c, trace builds)

## Validated by

The ring records _retaddr on every body entry. Positive: it named 0x00401ffd in the frame limiter on the first run that used it, and 0x1000cf4e / 0x0055b4a7 for the timer chain -- all three verified against the disassembly of the enclosing functions. Negative/limit: it names an ADDRESS, not a function -- only entry points are named, and a return address is by definition mid-function -- and it is 0 for host-side crossings, which the dump shows by printing nothing rather than a plausible zero.

## Known failure modes

(none recorded yet)
