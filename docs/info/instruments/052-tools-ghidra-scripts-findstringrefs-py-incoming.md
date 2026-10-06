---
id: I052
kind: instrument
status: trusted
created: 2026-08-14
---

## Instrument

tools/ghidra_scripts/FindStringRefs.py incoming reference and raw RTTI pointer walker.
`FIND_STRING_REFS` seeds a defined string plus its possible MSVC TypeDescriptor;
`FIND_ADDRESS_REFS` seeds one explicit in-image address.

## Validated by

On 2026-08-14, `restoreHealth` at 0x0068cd38 produced a DATA reference to
0x0068aa6c; `ECheckHealthEnergy` at 0x006d58ac produced zero ReferenceManager
edges while its TypeDescriptor start at 0x006d58a4 produced two independently
scanned aligned pointers at 0x006aab64 and 0x006aaba4. Seeding that same
0x006d58a4 through `FIND_ADDRESS_REFS` reproduced exactly those two pointers.
The cases prove the walker reports present database references, distinguishes
their absence, and recovers raw MSVC RTTI edges from both seed modes.

## Known failure modes

- It scans only defined Ghidra strings and aligned 32-bit pointers in initialized,
  non-executable memory blocks; packed, encoded, or runtime-built references remain
  outside its denominator.
- Backtracked addresses are candidate containing-structure starts, not proof that the
  candidate is a real object or that code consuming it is semantically related.
