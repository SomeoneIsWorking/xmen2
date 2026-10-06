---
id: I046
kind: instrument
status: trusted
created: 2026-08-12
---

## Instrument

X2_FILES=1 (kernel32 file trace)

## Validated by

Traced 53 file operations in a run whose own shutdown counter reported 939 opens over 368 distinct names: it sat on the CreateFile family alone while the CRT's fopen went past it, and a trace showing no Scripts/ and no Maps/ opens was read as 'the native run never reaches the level'. It reaches the menu and runs scripts/menus/intro_normal.py; that reading was wrong. Rebuilt to cover both paths, and its banner now states that repeat opens of an already-listed name are deliberately not shown and that the totals are in the shutdown report. Re-validated on a real run: 359 first-open lines including scripts/, packages/, maps/ and 103 NOT FOUND.

## Known failure modes

(none recorded yet)
