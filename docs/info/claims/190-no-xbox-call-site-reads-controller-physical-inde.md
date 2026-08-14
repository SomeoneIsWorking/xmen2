---
id: C190
kind: claim
status: holds
created: 2026-08-14
tags: input,xbox
---

## Claim

No Xbox call site reads controller physical index 8 or 9 (Black/White) through a LITERAL immediate at vtable slot +0x10. Of 884 such call sites across 22,931 disassembled functions, 323 push a literal last, and only 6 push 8 or 9 -- all 6 belong to a different class (the 15-bit flag setter sub_0005AD30, reached through singleton sub_0005B200), not the controller. The scan's blind spot is stated and large: 415 sites push a register and 146 push nothing within the 8-instruction window, so 561 of 884 sites are invisible to it. The conclusion is therefore narrow: the literal-index route to the Black/White trigger is EXHAUSTED, not that the trigger does not exist.

## Evidence

tools/xbe_query.py vslot 0x10 --imm 8 --imm 9 --histogram over vendor/xboxrecomp/tools/disasm/output/asm/text.asm; the 6 hits are sub_001E0780, sub_001E1D40, sub_001F13B0, sub_001F2680, each preceded by call sub_0005B200 whose vtable (0x00494B34) slot +0x10 is sub_0005AD30, a set/clear-bit on a 15-entry mask at +0xc -- not a float read.

## What would falsify it

a controller-typed object reaching slot +0x10 with a literal 8 or 9 anywhere the scan window missed, or a re-disassembly that detects functions the current text.asm does not
