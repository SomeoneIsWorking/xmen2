---
id: I052
kind: instrument
status: trusted
created: 2026-08-14
---

## Instrument

tools/ghidra_scripts/FindStringRefs.py incoming reference and raw RTTI pointer walker

## Validated by

Positive control restoreHealth at 0x0068cd38 produced a DATA reference to 0x0068aa6c; ECheckHealthEnergy at 0x006d58ac produced zero ReferenceManager edges but two independently scanned aligned pointers at 0x006aab64 and 0x006aaba4. The two answer classes prove the walker reports both present database references and absent ones while its raw-pointer path recovers MSVC RTTI edges.

## Known failure modes

- It scans only defined Ghidra strings and aligned 32-bit pointers in initialized,
  non-executable memory blocks; packed, encoded, or runtime-built references remain
  outside its denominator.
- Backtracked addresses are candidate containing-structure starts, not proof that the
  candidate is a real object or that code consuming it is semantically related.
