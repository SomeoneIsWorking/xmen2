---
id: C133
kind: claim
status: holds
created: 2026-08-06
tags: pc,recomp,rc-lift,ghidra
---

## Claim

A jump table's length comes from the switch's own range check, and the plausibility heuristic that replaced it cannot separate two adjacent tables

## Evidence

XMen2.exe 0x005fb240 holds 4 entries and 0x005fb250 holds 7, back to back, and all eleven dwords are valid in-block code addresses -- so RecreateFunction.py's 'take entries while they point into the same executable block' read straight through the first table into the second and wired 11 entries to the jump at 0x005facce. Its header comment had claimed for weeks that it kept adjacent tables apart. The consequence was not cosmetic: 0x005fafc1, a case of the OTHER switch (the jump at 0x005fafba in FUN_005fad31), was un-made as a case label of a container that could never absorb it, and the next run dispatched to it and found no body. MSVC guards a jump table with 'CMP <index>,N' / 'JA <default>' immediately before 'JMP [<index>*4 + <table>]', so the count is N+1: 'CMP EAX,0x3' -> 4 and 'CMP EAX,0x6' -> 7, exactly the 4+7 the heuristic read as 11. Implemented as caselabel.table_bound(), tested in tests/test_caselabel.py (ctest 'caselabel'), and which of the two decided a table is PRINTED so a read bound and a guessed one never look alike.

## What would falsify it

a switch in this image whose range check is 'JBE' into the table path rather than 'JA' past it, or one whose CMP immediate is not (count - 1) -- either would mean N+1 is not the count. Also falsified if a table bounded this way turns out SHORTER than the case labels the runtime actually reaches.
