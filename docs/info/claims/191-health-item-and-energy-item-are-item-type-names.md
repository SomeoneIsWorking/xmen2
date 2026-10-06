---
id: C191
kind: claim
status: holds
created: 2026-08-14
tags: input,xbox
---

## Claim

HEALTH_ITEM and ENERGY_ITEM are ITEM-TYPE names, not event names: they are entries 0 and 1 of a pointer-to-string table at 0x0053FEBC in the Xbox build, followed by XTREME_PIP, SKIRMISH_KING_PIP, KEY1..KEY3 and KEYCARD1..KEYCARD3. Neither string is referenced by any instruction in the disassembly -- both are reached only through that table -- so a search for code that 'names HEALTH_ITEM' cannot find the pack-use path. Xbox sub_00088680 (the counterpart of PC FUN_0047a140) also has zero recorded direct callers and contains no input query of its own; it is an event/virtual handler that receives an object, which is consistent with the PC owning the consumption logic.

## Evidence

tools/xbe_query.py strtab 0x0053FEBC --count 12; grep of text.asm for 0x4977ac/0x4977b8 finds no instruction operand; xbe_query.py find 0x004977AC / 0x004977B8 finds exactly one occurrence each, both in .data; xbe_query.py func 0x88680 --callers reports 0 sites, and its body's indirect calls are all through slots other than a controller read.

## What would falsify it

an instruction operand referencing 0x004977AC/0x004977B8, or a direct call to sub_00088680 appearing after a re-disassembly
