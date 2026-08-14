---
id: I053
kind: instrument
status: trusted
created: 2026-08-14
---

## Instrument

tools/xbe_query.py -- static queries against the Xbox default.xbe and its disassembly (read/find/vtable/strtab/func/vslot)

## Validated by

Its own selftest runs each query against a case that MUST come out positive AND one that MUST come out negative: an unmapped VA refuses instead of reading zeros; a value chosen to be absent reports 0 occurrences WITH the 5,681,344-byte denominator; a code address does not resolve as a string; an address inside a function is not a function start. 8 of 8 checks pass on 2026-08-14. vslot prints the share of call sites its literal scan CANNOT see (561 of 884 at slot +0x10), so its negatives carry their blind spot. Missing image or disassembly exits 77/2, never an empty result.

## Known failure modes

(none recorded yet)
