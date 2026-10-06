---
id: I025
kind: instrument
status: trusted
created: 2026-08-06
---

## Instrument

Per-chunk -O0 for exact fault lines (-DX2_NATIVE_O0=<chunk>.c)

## Validated by

Validated by agreement rather than by assumption: the -O0 build reported the SAME line (libIGCore_003.c:79777) as the -O2 build for the issue-15 fault, and the register dump then confirmed that line's operand (edi=0) matched the fault address. So -O2 attribution was correct here -- which is the useful finding, since it means the cheap build can be trusted for this class of fault. CMake FATAL_ERRORs on a chunk name that does not exist, so a typo cannot silently produce an ordinary -O2 build that is then read as exact.

## Known failure modes

(none recorded yet)
