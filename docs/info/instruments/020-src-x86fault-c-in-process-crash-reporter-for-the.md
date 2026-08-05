---
id: I020
kind: instrument
status: trusted
created: 2026-08-05
---

## Instrument

src/x86fault.c -- in-process crash reporter for the recompiled PC modules: names the module a fault EIP falls in, annotates stack slots with the call site that pushed them, and dumps a ring of the last 64 recompiled/host boundary crossings

## Validated by

Built because winedbg attached under 'wine explorer /desktop=' produces NO output at all -- measured: scratch/logs/ark-dbg.log is four lines of Wine banner and nothing else. Positive control: X2_FAULT_SELFTEST=1 raises a private exception code and the handler reports seeing it before the game runs (observed). Negative control: with no fatal exception the exit report says so in those words, with the count of non-fatal first-chance exceptions it DID see, so silence cannot be misread as 'no handler'. It named the root cause of C080 on its first real run.

## Known failure modes

(none recorded yet)
