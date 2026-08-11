---
id: C149
kind: claim
status: holds
created: 2026-08-11
tags: lift,tooling
---

## Claim

A module's own RELOCATION TABLE enumerates every absolute code pointer in it, so the runtime discovery loop is not the mechanism for finding indirect-call targets -- it is the check that the static pass was complete

## Evidence

Every absolute address baked into a relocatable image has an IMAGE_REL_BASED_HIGHLOW entry, because the loader must fix it up if the image moves. Measured on the shipped modules: msdia80 4396 relocation values land in .text, libIGCore 2307, libIGOpt 1764. Both targets the discovery loop found in msdia80 one round at a time (0x103f06fa, 0x103f0715) were reloc values. Seeding msdia80 from them created 1382 functions in ONE pass (3442 -> 4824 functions, 149k -> 169k instructions) and the run then cleared msdia80 entirely, returning to the pre-existing frontier (KERNEL32!SuspendThread in libCriMovie) with no missing body anywhere. The two older bulk seeders both guess and both under-approximate: SeedPointerTables.py needs THREE consecutive aligned dwords in read-only data (a lone callback field is invisible to it, and .data needs an opt-in), seed_code_imms.py reads immediates out of instruction text and sees nothing that lives in data.

## What would falsify it

a module in which the runtime still reports a missing dispatch target AFTER a reloc-seeded pass. Such a target is by construction one with no absolute address in the file -- computed at run time (base+index, an RVA table, a switch's own jump table) -- and would mean the reloc set is a floor rather than the whole answer. An EXE linked /FIXED has no relocation directory at all, and seed_relocs.py refuses for it rather than reporting nothing found.
