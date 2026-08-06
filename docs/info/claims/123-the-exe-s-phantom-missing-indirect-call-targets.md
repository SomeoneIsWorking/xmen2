---
id: C123
kind: claim
status: holds
created: 2026-08-06
tags: pc,recomp,rc-lift,rc-exe,switch
reconfirmed: 2026-08-06
---

## Claim

The exe's phantom 'missing indirect call targets' are SWITCH CASE LABELS: the recompiler dispatches an indirect JMP through a jump table as if it were a call

## Evidence

Read out of the binary, decisively. FUN_005fac10 ends with JMP dword ptr [EAX*0x4 + 0x5fb240] -- an MSVC switch dispatch -- and the table at 0x005fb240 (which sits in the data immediately after the function's last instruction at 0x005fb23c) contains 0x005facd5, 0x005face5, 0x005facf6, 0x005fad07, 0x005fafc1, 0x005fafd5. A second table at 0x005fb250 holds six more. EVERY entry is inside the function's own range. 0x005facd5 and 0x005face5 are precisely the two addresses the discovery loop kept reporting as unresolved indirect-call targets and kept seeding.

THE MECHANISM, end to end: the runtime dispatches the indirect JMP as a call, so a case label is reported as 'no recompiled body at <addr>'; the discovery loop seeds it; Ghidra makes it a function, carving the switch statement apart; the fragment then executes to the real function's epilogue and its RET pops whatever the parent's frame held, which surfaces as 'x86_return_to: <addr> is not a function entry'. Every symptom in issue #21 is one of those steps. Observed cycling three times in this session: seed 0x005fb270 -> stop at 0x005facd5 -> seed that -> FUN_005facd5's RET pops 0x005fb2bc, which is the symptom it started from.

This also explains why the exception-handling readings were dead ends. The function IS an MSVC C++ EH function, and that is a coincidence of the same function: its FuncInfo has nTryBlocks = 0 and its unwind actions are elsewhere (0x679110-0x679128), so no funclet was ever involved.

## What would falsify it

Finding a reported 'missing indirect target' in this exe that is NOT an entry of any jump table -- i.e. scanning every JMP dword ptr [reg*4 + <imm>] in the image, collecting the tables' contents, and finding a runtime-reported address absent from that set. The two addresses checked here were both present; a third that is not would show this is only part of the story.

## Re-confirmed 2026-08-06

REFINED, and the fix is cheaper than the claim first said. recomp.py ALREADY handles this correctly: fn['_has_injmp'] is set when a function contains an indirect JMP, and it then emits a C label for EVERY instruction address in the function plus an L_injmp switch mapping module-relative offsets to those labels, falling through to the global dispatcher only for genuine tail calls out of the module. A whole function with a switch therefore resolves its own case labels internally and never reaches the dispatcher.

So the translator is not the defect. The defect is that the function is not whole: once the discovery loop seeds a case label and Ghidra carves the function, the label is no longer among fn['_addrs'], the L_injmp switch has no case for it, and it falls through to DISPATCH -- which reports 'no recompiled body'. The carve creates the very symptom that prompts the next carve.

CORRECTION to the previous note and commit, which said recomp.py must be changed: it must NOT. The fix is entirely in native_discover.sh -- refuse to seed any address that appears in a jump table -- plus repairing the damage already done in this region. That is a much smaller change and it does not touch the translator.

Also noted for whoever does it: the tables at 0x005fb240 and 0x005fb250 are adjacent, so a fixed-count read of the first runs into the second. Their lengths have to come from the switch's own bound check (the JA above the JMP), not be assumed.
