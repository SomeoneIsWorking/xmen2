---
id: C076
kind: claim
status: falsified
created: 2026-08-05
tags: xbox,recomp,disasm,jump-table
falsified_on: 2026-08-05
---

## Claim

The remaining boot blocker is DECODER DESYNC on jump tables interleaved with code, not the function boundary and not the table reader. memcpy (sub_003D5890) stores its switch tables between its own basic blocks; a linear decode walks into the table at 0x3D5970, decodes the pointer bytes as instructions, and stops at 0x3D5996 -- so the dispatch instructions at 0x3D5A4B/0x3D5A56/0x3D5A71 are never decoded at all. Nothing downstream can recover from that: the boundary walker cannot follow a jump it never saw, and the recompiler cannot emit labels for targets nobody detected.

## Evidence

Decoded directly from the shipped XBE: DisasmEngine.decode_from(0x003D5890, 0x003D5B00) yields 85 instructions ending at 0x3D5996, with 'cmp eax, 0x3d59a000' and 'in al, 0x89' where the table bytes are. get_instruction(0x003D5A56) returns None. Separately measured: with the table displacement parsed from the operand, 371 tables are readable and 38 functions extend past their detected end -- memcpy is not among them, because its dispatch is never decoded.

## What would falsify it

A decode of sub_003D5890 that reaches 0x003D5A56 without treating the table region as data -- which would mean the desync has another cause; or the run getting past 0x003D5B54 without the tables being enumerated.

## FALSIFIED 2026-08-05

WRONG as stated, and the error was my instrument, not the binary. I claimed memcpy's dispatch instructions at 0x3D5A4B/56/71 are 'never decoded at all', evidenced by an ISOLATED DisasmEngine.decode_from(0x3D5890, 0x3D5B00) that desynced on the table. The REAL pipeline runs a full linear_sweep of .text, which resynchronises and DOES decode all three -- get_instruction returns them. The numbers I derived from the same isolated decode (371 tables readable, 38 functions extended) were wrong too; measured against pipeline state they are 358 and 13. And memcpy's boundary DID extend, to 0x3D5BCD -- I reported otherwise from a stale functions.json. ACTUAL cause: MSVC indexes these tables with a NEGATIVE register, so entries sit BELOW the displacement -- memcpy's jmp [ecx*4 + 0x3d5b28] has its six entries at 0x3d5b14..0x3d5b28. The reader walked forward only, found one of six, and the data-span it handed the re-decoder covered the wrong bytes, so the table stayed decoded as instructions and the targets never got labels. Lesson: measure through the pipeline, not through a hand-built decode of one function.

> Anything that cited this claim as proof must be re-checked. Grep the repo for it.
