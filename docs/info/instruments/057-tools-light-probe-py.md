---
id: I057
kind: instrument
status: trusted
created: 2026-08-15
---

## Instrument

tools/light_probe.py

## Validated by

Finds the engine's light records in a LIVE process (process_vm_readv, no debugger, no perturbation) so the port and the Wine control can be read by ONE instrument and compared as numbers. It was WRONG first and the record matters: validated against random bytes it showed 0 false positives per MiB, then matched 6,055,400 times in a live port and found 7,999 'lights' in the control -- because real program memory is full of 0.0 and 1.0 floats, so random noise was the wrong negative class. It also scanned 0.8 MiB of a multi-GiB process while reporting five findings as though that were the answer. Both are fixed: coverage is now printed as a percentage with the skipped regions listed, and the signature is a RUN of records at the 140-byte stride the engine actually uses -- confirmed from the port's own X2_LIGHT_ADDR output, where light index 6 sits at 0x04674cd4 and index 7 at 0x04674d60, exactly 0x8c apart. The port's reported address is passed as --expect so a scan says in words whether it found the record that MUST be there.

## Known failure modes

(none recorded yet)
