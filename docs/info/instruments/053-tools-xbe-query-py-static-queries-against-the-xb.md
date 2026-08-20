---
id: I053
kind: instrument
status: trusted
created: 2026-08-14
---

## Instrument

tools/xbe_query.py -- static queries against the Xbox default.xbe and its disassembly (read/find/vtable/strtab/func/vslot/chain/aftercall)

## Validated by

Its own selftest runs each query against a case that MUST come out positive AND one that MUST come out negative: an unmapped VA refuses instead of reading zeros; a value chosen to be absent reports 0 occurrences WITH the 5,681,344-byte denominator; a code address does not resolve as a string; an address inside a function is not a function start; present/absent vtable slots and literal filters separate; and `chain`/`aftercall` attribute known accessors while rejecting absent ones. 17 of 17 checks pass on 2026-08-20. `vslot` states that numeric register slots are class-agnostic and prints the share its literal scan CANNOT see. `chain` and `aftercall` preserve object provenance from virtual and direct accessors respectively. Missing image or disassembly exits 77/2, never an empty result.

## Known failure modes

`vslot` cannot establish class identity from a numeric slot alone; use `chain`
or `aftercall` when object provenance is available. On 2026-08-20,
`vslot --imm ... --show-sites` was caught printing every site because the
detail flag bypassed the literal selection. Selection now has positive and
negative selftests.
