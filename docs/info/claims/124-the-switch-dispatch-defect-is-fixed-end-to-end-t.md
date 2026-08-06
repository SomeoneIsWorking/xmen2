---
id: C124
kind: claim
status: holds
created: 2026-08-06
tags: pc,recomp,rc-lift,rc-exe,graphics
---

## Claim

The switch-dispatch defect is FIXED end to end: the recompiled exe now runs past it and reaches the game's own display-initialisation failure

## Evidence

Three fixes, each verified, and the run moved as a result. (1) recomp.py's _has_injmp gated on the exporter's 'ind' flag, which Ghidra never sets on JMP dword ptr [reg*4 + <table>]; widened to 'ind OR no resolved flow', pinned by tests/test_recomp.py which fails 4 of 6 on the old predicate naming the emitted DISPATCH(C, RD32(...)) line and passes in 8ms on the new. (2) Ghidra had not resolved the computed jump at all, so no references left the JMP and FUN_005fac10's body had HOLES where its own case labels were -- 426 instructions spanning 0x005fac10..0x005fb23c but containing none of 0x005facd5/0x005facf6/0x005fad07. RecreateFunction.py now reads the table, adds a COMPUTED_JUMP reference per entry, disassembles the targets and re-creates the function: 11 and 7 entries wired from the tables at 0x005fb240 and 0x005fb250, body 426 -> 477 instructions (+51), and its own postcondition reports 'all 4 expected address(es) are now inside a function'. On the run before the table was handed over the same script reported '+0, -0' and 'POSTCONDITION FAILED', so the negative was visible rather than reading as a repair.

RESULT, on a real --vk run: no 'no recompiled body' and no 'x86_return_to' anywhere. The run executes the switch and reaches XMen2.exe's OWN error dialog -- 'Display failed! Unable to initialise graphic display. Resolution and FSAA have been reverted to default.' That is the game's code deciding the renderer did not come up, which is renderer work rather than recompiler work, and it is the furthest this port has run.

## What would falsify it

A later run reporting 'no recompiled body' at an address that is an entry of a jump table, which would show the resolution is incomplete for other switches in the image -- only FUN_005fac10's two tables have been wired, and nothing has swept the module for the rest.
