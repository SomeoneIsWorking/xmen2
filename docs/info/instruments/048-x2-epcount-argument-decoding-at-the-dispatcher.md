---
id: I048
kind: instrument
status: trusted
created: 2026-08-13
---

## Instrument

X2_EPCOUNT argument decoding at the dispatcher

## Validated by

DISTRUSTED, and reverted. The passive counter is sound and has run repeatedly; extending it to decode each dispatched call's arguments as strings killed the run at 12 s with SIGSEGV at 0x6f6c6e75 -- ASCII 'unlo', the guest executing a string -- with EDX in .rdata beside the script-path constants. The same build with the counter switched off ran to completion. One run each way, but the diagnostic was the only variable. Reading arguments at the dispatcher must not be re-landed until the mechanism is understood.

## Known failure modes

(none recorded yet)
