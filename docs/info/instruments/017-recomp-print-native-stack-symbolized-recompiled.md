---
id: I017
kind: instrument
status: trusted
created: 2026-08-05
---

## Instrument

recomp_print_native_stack() -- symbolized recompiled call stack on any indirect-call failure

## Validated by

Verified on the real run 2026-08-05: the ICALL fatal path used to print only the failing address and a ring buffer of recent targets, so a call through a NULL pointer named nothing but itself and the one question that mattered -- who was about to call it -- had no answer in the log. It now resolves each return address with dladdr (needs -rdynamic) exactly as the crash handler does, and named sub_002A9570+0x1066 with four callers above it on the first run. Prints frames-resolved-of-slots-scanned, so a short trace cannot be read as a thorough one. Caveat: it scans raw stack slots, so unrelated symbols (g_icall_trace, stderr, clock_gettime) appear between real frames -- read the sub_XXXXXXXX entries, ignore the rest.

## Known failure modes

(none recorded yet)
