---
id: I044
kind: instrument
status: trusted
created: 2026-08-11
---

## Instrument

tools/seed_relocs.py -- every absolute code pointer in a module, read out of its own relocation table

## Validated by

Validated against BOTH classes. POSITIVE: --selftest builds a PE32 in memory whose single relocation points at a dword holding a .text address and asserts the parser returns exactly that address (a reloc parser that silently returns nothing would make every caller read 'no candidates' as 'fully covered'). NEGATIVE: it REFUSES, non-zero, for an image with no relocation directory, saying it searched NOTHING rather than reporting an empty result. On real data it found 51 uncovered code pointers in libIGCore, 5 in libIGOpt and 3071 in msdia80; seeding msdia80 with them created 1382 functions in ONE pass, where the runtime discovery loop had been finding exactly one per round. Its output is a CANDIDATE set, not a claim about code: a pointer to a read-only table or a string that MSVC placed in .text relocates exactly like a function pointer, and 1587 of msdia80's 3071 were rejected by AddFunctions.py as defined data (Ghidra's code/data separation, which is the authority on that), 62 failed to disassemble.

## Known failure modes

(none recorded yet)
