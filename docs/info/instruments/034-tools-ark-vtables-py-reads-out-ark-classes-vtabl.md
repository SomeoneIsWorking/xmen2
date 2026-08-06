---
id: I034
kind: instrument
status: trusted
created: 2026-08-06
---

## Instrument

tools/ark_vtables.py -- reads out ARK classes' vtable address, slot count and override structure

## Validated by

The slot count is bracketed from BOTH sides on the class that matters, not walked until it looked done. igDx8VisualContext: the array ends exactly where igDx8VertexStream's registered vtable begins (334 slots), and slot 333 is independently shown to be live by a CALL dword ptr [reg + 0x534] elsewhere in the module -- upper and lower bound meet at 334. Two length estimates (code-scan vs next-neighbour) are computed for every class and every DISAGREEMENT is printed, which is how the first version was caught: bounding slots by 'points into an executable section' overran the array end into trailing data, and using only ARK-registered classes as neighbours overshot because abstract classes emit vtables ARK never names. Both were fixed -- slots must point at a DECODED FUNCTION ENTRY, and boundaries are harvested from every vptr store in the module (193 found vs 77 registered). The pure-virtual stub it relies on was confirmed rather than assumed: 0x100ce258 is JMP dword ptr [0x100cf17c], which the .iat resolves to MSVCRT _purecall. Known limit: 77 of 77 vtables still show a code-scan/neighbour disagreement, so per-class counts other than the bracketed ones should be treated as bounds, not facts.

## Known failure modes

(none recorded yet)
