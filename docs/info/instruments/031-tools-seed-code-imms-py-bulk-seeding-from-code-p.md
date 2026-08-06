---
id: I031
kind: instrument
status: trusted
created: 2026-08-06
---

## Instrument

tools/seed_code_imms.py: bulk seeding from code-pointer immediates

## Validated by

Validated by producing a large, non-uniform, checkable answer where the alternative produced one per round: 138 new function starts in libIGGfx, 988 in XMen2.exe, 6 in libIGCore, 5 in libIGAttrs, 1 in libIGMath. Confirmed against the shipped binaries -- the byte pattern at each site is 68 <addr> (PUSH imm32) followed by a CALL through an import, i.e. a callback handed to a registrar, which is exactly why no reference-driven pass can see it. After seeding, libIGGfx went 4747 -> 4876 functions and XMen2.exe 14893 -> 15881. Every rejection class is COUNTED and printed -- already-a-function, not-executable, and inside-an-existing-function (which needs a SPLIT, not a seed, and is listed by address) -- so a run that found little cannot be confused with one that silently dropped most of it. It REFUSES outright if the module has no executable block, rather than reporting zero candidates from nothing.

## Known failure modes

(none recorded yet)
