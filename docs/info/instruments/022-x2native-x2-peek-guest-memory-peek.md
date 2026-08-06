---
id: I022
kind: instrument
status: trusted
created: 2026-08-06
---

## Instrument

x2native X2_PEEK guest-memory peek

## Validated by

Reads via process_vm_readv rather than a dereference, so it is safe from the SIGSEGV handler and an unmapped address reports 'UNREADABLE (not mapped)' instead of faulting again. Validated in both directions on the issue-14 run: XMen2+0x27f708 read back 0x2415f3fc (a plausible mapped address) while an unresolvable module name reports that nothing was read. Module+offset form resolves through the module's ACTUAL mapped base, which is the only form a Ghidra address can be pasted into.

## Known failure modes

(none recorded yet)
