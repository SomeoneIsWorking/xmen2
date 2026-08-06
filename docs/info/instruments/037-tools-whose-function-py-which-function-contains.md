---
id: I037
kind: instrument
status: trusted
created: 2026-08-06
---

## Instrument

tools/whose_function.py -- which function contains an address, and whether splitting there would destroy an SEH-protected function

## Validated by

Run against BOTH classes, not reasoned about. POSITIVE: 0x005fac25, an address interior to FUN_005fac10, is flagged '<<< SEH PROLOGUE -- DO NOT SPLIT', prints the function's first three instructions (PUSH -0x1 | PUSH 0x679141 | MOV EAX,FS:[0x0]) and exits 1. NEGATIVES: 0x00500000 (in no detected function) and 0x005facd5 (is itself a function entry) are both reported as such and exit 0 -- and the two negatives are distinguished from each other, because 'creates a function' and 'nothing to carve' need different responses. It refuses with a non-zero exit when given no addresses rather than printing a clean bill of health for an empty list. Wired into native_discover.sh ahead of every split escalation, which previously carved functions with no output at all -- the failure recorded as issue #21.

## Known failure modes

(none recorded yet)
