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

## Verified tree-wide, 2026-08-11

One `native_discover.sh` run with relocation seeding in the bulk stage, over all
20 exported modules: cg 540 candidates, libIGGfx 411, cgD3D8 231, msdia80 1685,
libIGLua 114, libCriMovie 112, libIGCollision 52, libIGCore 51, libIGViewer 41,
libIGInsight 39, libIGMath 27, libIGAudio 19, libIGSg 19, libIGDisplay 8,
libIGGui 5, libIGOpt 5, libIGUtils 1. **Round 1 then found no missing
constructor targets at all**, and the run's stop is a missing IMPORT
(`KERNEL32!SuspendThread`, libCriMovie) rather than a missing body -- the
falsifier's condition did not occur anywhere in the tree.

XMen2.exe is the stated exception and behaved as stated: no relocation
directory (linked /FIXED), so `seed_relocs.py` REFUSED for it and said it had
searched nothing. The exe keeps `seed_code_imms.py` and the runtime loop.

Several modules had every candidate rejected as defined data (libIGGui 5 of 5,
libIGOpt 5 of 5, libIGUtils 1 of 1, libIGDisplay 7 of 8) -- which is the guard
doing its job, not a failure: those are pointers to read-only tables the
compiler placed in an executable section.
