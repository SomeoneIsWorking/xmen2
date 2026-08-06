---
id: 21
title: Splitting FUN_005fad31 at the address its RET popped makes a bogus function, it does not fix the stack imbalance
status: resolved
symptom: x86_return_to: 0x005fb2bc is not a function entry -- a RET popped something that is not a return address. The RET is in 0x005fad31 (FUN_005fad31), which was ENTERED with 0x000c0260 on the stack and left with 0x005fb2bc there.
tags: pc,recomp,native,rc-exe,rc-lift,dead-end
created: 2026-08-06
updated: 2026-08-06
---

## What was tried

`x86_return_to` reported XMen2.exe `FUN_005fad31`'s RET popping `0x005fb2bc`
instead of its own return address. That is the issue #8 signature -- a
detected function body that swallowed an interior helper -- and the standard
remedy in this repo is a split, so `tools/ghidra_export.sh XMen2 --split-at`
was run at `0x005fb2bc`.

## Why it is a dead end

**The split succeeded and produced a function that is not one.** Ghidra
reported "1 split, 0 failed", and the new `FUN_005fb2bc` begins:

    005fb2bc  MOV EDI,dword ptr [EAX]
    005fb2be  TEST EDI,EDI
    ...
    005fb2d2  PUSH EBP

No prologue, and a `PUSH EBP` with no matching entry-side pop -- this is a
continuation block reached by a jump, not a call target. Re-running only moved
the symptom one hop along: `FUN_005fb2bc`'s RET then popped `0x00401812`,
which is not a function entry either.

Reverted with `--merge`.

## What this means for the real cause

`0x005fb2bc` being an address INSIDE the function whose RET popped it is the
tell. The function is pushing an address of its own and not popping it -- a
`push <label>; jmp` idiom, an SEH funclet, or a jump table -- and the
translator is mishandling that construct. The boundary was never the problem,
so no amount of splitting or seeding will converge.

This is an `rc-lift` defect, not a renderer one, but it is what currently
stops the `--vk` run: the renderer now constructs and tears down cleanly and
the run dies here, in the exe, before any frame is driven.

## What to do instead

Disassemble `FUN_005fad31` in full and find where it pushes `0x005fb2bc`.
Compare what `recomp.py` emitted for that instruction against the original.
Do not split, and do not seed.

### Note (2026-08-06)
SHARPER DIAGNOSIS, and it points at the discovery loop rather than at any one function.

`FUN_005fad31` is ITSELF not a function. It begins `LEA EDX,[ESP + 0x18]; PUSH 0xf; PUSH EDX; CALL ...` -- no prologue, and it reads a stack slot at ESP+0x18 that no prologue of its own established. And searching its 364 instructions finds NOTHING that pushes 0x005fb2bc, so the address its RET popped is not even produced inside it.

So the region around 0x005fac10-0x005fb2bc is a single real function that has been repeatedly carved up. Commit 8b15fec already recorded one such carve here: seeding 0x005face5 shrank FUN_005fac10 from 1390 bytes to 197. The two `native_discover.sh` rounds in this session seeded two more XMen2.exe addresses.

**The systemic worry**: the discovery loop seeds addresses the runtime reports as unresolved INDIRECT-call targets. If such a target is not really a function entry -- because the indirect call was itself mistranslated, or the value came from a mis-lifted jump table -- then seeding manufactures a fake function at it, and Ghidra carves a real function in half to make room. That failure is self-reinforcing: the carved halves have unbalanced stacks, which produces more bogus `x86_return_to` targets, which look like more seeds.

That hypothesis is testable and should be tested before any more seeding in this region: take the addresses this loop has seeded into XMen2.exe and check each for a real prologue. Any that begins mid-flow was manufactured.

Until then, treat `native_discover.sh` seeds in XMen2.exe as suspect, and note that the loop currently has NO check that an address it seeds looks like a function entry -- adding one would refuse the bad seed instead of silently damaging the database.

### Note (2026-08-06)
ROOT CAUSE FOUND, and it is one function, not a class of them.

Checking every address seeded or split in this region for a real prologue:

    0x005fac10  FUN_005fac10   56 ins  PUSH -0x1 | PUSH 0x679141 | MOV EAX,FS:[0x0]
    0x005facd5  FUN_005facd5    5 ins  PUSH EBP | PUSH 0x6a3d08 | PUSH 0x4
    0x005face5  FUN_005face5    5 ins  PUSH 0x2 | PUSH 0x6a3d08 | PUSH 0x4
    0x005fad31  FUN_005fad31  364 ins  LEA EDX,[ESP + 0x18] | PUSH 0xf | PUSH EDX
    0x005fb2bc  FUN_005fb2bc  152 ins  MOV EDI,[EAX] | TEST EDI,EDI | MOV [ESP+0x1c],0

Only the FIRST is a function. `PUSH -1; PUSH <handler>; MOV EAX,FS:[0]` is the
textbook MSVC **SEH prologue** -- it installs an exception frame on the stack.
The other four all begin mid-flow.

So 0x005fac10 is ONE SEH-protected function and the discovery loop has carved
it into five pieces across three sessions. That explains the symptom exactly:
the SEH prologue pushes an exception registration record, and a fragment
starting after it RETs into that record's contents instead of a return
address. `0x005fb2bc` and `0x00401812` are not corrupted return addresses --
they are pieces of the SEH frame.

Confirmed from the loop's own files: `scratch/recomp/XMen2.seeds` still
contains exactly `0x005fad31`, so the loop did manufacture that entry.

## Why the loop does this, and what it should do

The loop seeds addresses the runtime reports as unresolved indirect-call
targets. Inside an SEH function, the addresses the code pushes are handler and
scope-table pointers, and a mistranslated frame makes them look like call
targets. The loop then finds them 'already inside a function' and ESCALATES TO
A SPLIT, which assumes the runtime is right and the database is wrong. Here it
is the other way round.

The escalation is not always wrong -- commit 8b15fec records a case where the
split was correct -- so it should not simply be removed. What it must not do
is carve silently. A seed that lands inside a function whose entry has an SEH
prologue is the case to refuse, and the loop should say which function it is
about to carve and what that function's first instructions are.

### Note (2026-08-06)
REPAIRED, and the repair makes the run stop EARLIER -- which is correct, not a regression.

Four `--merge` rounds reassembled the function. FUN_005fac10 went from 56 instructions to 426. Rounds 3 and 4 both reported '1 skipped (the inner function is real)' for the 5-instruction 0x005face5, so the merge tool's own guard declined to absorb it; that fragment is left alone deliberately.

With correct boundaries the run now stops BEFORE the renderer, on an indirect call to 0x005fb270 -- an address INSIDE the repaired function, reported as 'dispatch target with no recompiled body'. Previously, with the function carved up, 0x005fb270 happened to be a fragment entry and the call resolved, so the run got as far as setVideoMode.

That is the confirmation, not a setback. The carved database was letting a mistranslated indirect call resolve by accident. The real defect is that something computes 0x005fb270 as a call target at all, and it is inside an SEH-protected function -- so the suspicion in the note above (the target is a handler or scope-table pointer from a mishandled exception frame, not a function) now has a second instance supporting it.

**Do not seed 0x005fb270.** That is the loop's instinct and it is what produced this issue. `tools/whose_function.py` will now flag it, since it falls inside a function with an SEH prologue.

The next step is to read how recomp.py translates FUN_005fac10's SEH prologue -- `PUSH -1; PUSH 0x679141; MOV EAX,FS:[0]` and the matching `MOV FS:[0],ESP` -- and the indirect call that produces 0x005fb270. This is an rc-lift defect and it is now the single thing standing between the renderer and its first frame: everything on the renderer side is in place and the host frame path is verified to present (see the vk_frame_path test).

### Note (2026-08-06)
CLASS IDENTIFIED -- C122. These are MSVC C++ **catch funclets**, and that changes what the fix is.

FUN_005fac10's handler, 0x679141, is `MOV EAX,0x6c3d58; JMP 0x0067208c`, and 0x0067208c is `JMP dword ptr [0x0067f154]` which XMen2.iat resolves to **MSVCR71.dll __CxxFrameHandler**. That is the canonical C++ EH stub, so this function has try/catch and 0x6c3d58 is its FuncInfo. 0x005fb270 is an address inside the function that Ghidra places in no function -- a catch funclet, called indirectly by MSVCR71's unwinder from the FuncInfo TryBlockMap.

**Both halves of the trap now have names:**
  * A funclet is invisible to static analysis, because its only reference is a DATA table. That is why the discovery loop keeps reporting it -- the loop is doing its job.
  * A funclet is NOT a function. It runs on its PARENT's frame. Seeding it as one carves the parent, which is what produced this issue.

So the loop's escalation is wrong here for a structural reason, not a heuristic one, and `whose_function.py`'s SEH-prologue flag is catching the right thing.

**What to build instead of seeding**: parse the FuncInfo tables and enumerate funclets as a known category. Every `MOV EAX,<imm>; JMP <__CxxFrameHandler thunk>` stub in the image names a FuncInfo; each FuncInfo's TryBlockMap names its catch handler addresses. That turns 'mysterious indirect target' into 'catch funclet of function F', which the recompiler can then model deliberately -- and it enumerates them all at once instead of one per discovery round.

C122 records the falsifier: the FuncInfo parse itself is UNVERIFIED (a throwaway script hit a VA-to-file-offset bug), so confirming 0x005fb270 appears in that TryBlockMap is step one.

### Note (2026-08-06)
C122 TESTED AND FALSIFIED -- record the dead end so nobody builds the tool it proposed.

The funclet hypothesis was checkable and it is wrong. Parsing the FuncInfo at 0x6c3d58 (magic 0x19930520, so it IS a real C++ EH FuncInfo):

  * nTryBlocks = 0, TryBlockMap = NULL. The function has no catch handlers at all, so 0x005fb270 is not a catch funclet.
  * UnwindMap at 0x006c3d38, maxState 4. Its four actions are 0x00679110 / 0x00679118 / 0x00679120 / 0x00679128 -- small thunks beside the handler stub. 0x005fb270 is not among them, so it is not a destructor funclet either.

So the 'enumerate funclets from the FuncInfo tables' tool proposed in the previous note would NOT have found this address and should not be built for this reason.

**What still stands**: FUN_005fac10 is genuinely an MSVC C++ EH function, and seeding inside it genuinely carves it apart. Everything this issue records about the discovery loop is unaffected.

**Where it points now**, and it is the simpler reading: the exception-frame prologue -- `PUSH -1; PUSH <handler>; MOV EAX,FS:[0]` and the matching `MOV FS:[0],ESP` -- is mistranslated, so state is already wrong when an indirect call is reached, and 0x005fb270 is a garbage target that merely happens to land inside the same function. A consequence, not a cause.

**Next**: read what recomp.py emits for those FS-segment instructions and for the indirect call in FUN_005fac10, and compare against the original. Do not seed, do not split, and do not go looking for EH tables.

### Note (2026-08-06)
THIRD HYPOTHESIS CHECKED -- the exception-frame prologue is NOT mistranslated either.

recomp.py emits FS-relative access through the runtime's FS base, and src/native/x2native.c's tib_init maps a flat TIB block at 0x000A0000 with the 0xFFFFFFFF end-of-chain sentinel. `MOV EAX,FS:[0]` and `MOV FS:[0],ESP` therefore read and write real, owned memory. The prologue works; it is not the corruption source.

(One thing WAS wrong and is fixed: recomp.py's comment there claimed 'we run as a real 32-bit PE, so FS still addresses the TIB and the exception chain there is the genuine one'. True under Wine, false for x2native, and it contradicted x86rt.h's own comment two files away. Corrected to name both hosts and to repeat the gap both files already state -- the chain is well-formed but nothing DELIVERS an exception if the guest throws.)

So three readings of 0x005fb270 have now been tested and refuted: catch funclet (no try blocks), destructor funclet (not in the unwind map), mistranslated exception prologue (the prologue is fine).

**What has NOT been checked, and is where to start next**: the indirect call site itself. Find which instruction in FUN_005fac10 performs the indirect call, read the C recomp.py emitted for it and for whatever computes its target register, and compare against the original. Everything upstream of the call site has been eliminated; the call site has not been looked at once.

### Note (2026-08-06)
ROOT CAUSE, read out of the binary -- C123. They are SWITCH CASE LABELS.

FUN_005fac10 ends with `JMP dword ptr [EAX*0x4 + 0x5fb240]`, an MSVC switch dispatch. The table at 0x005fb240 -- data sitting immediately after the function's last instruction at 0x005fb23c -- contains:

    0x005facd5  0x005face5  0x005facf6  0x005fad07  0x005fafc1  0x005fafd5

and a second table at 0x005fb250 holds six more. Every entry is inside the function's own range, and **0x005facd5 and 0x005face5 are exactly the two addresses this loop kept reporting and seeding.**

## The whole chain, finally

1. The runtime dispatches the indirect JMP as if it were a CALL, so a case label is reported as 'no recompiled body at <addr>'.
2. The discovery loop, doing its job, seeds it.
3. Ghidra makes it a function -- carving the switch statement apart.
4. The fragment runs on to the real function's epilogue, whose RET pops whatever the parent's frame held: 'x86_return_to: <addr> is not a function entry'.

Every symptom recorded in this issue is one of those four steps. Watched it cycle three times in one session: seed 0x005fb270, stop at 0x005facd5, seed that, and FUN_005facd5's RET pops 0x005fb2bc -- the symptom this issue opened with.

## Why the exception-handling detours were dead ends

The function IS an MSVC C++ EH function, and that is a coincidence of the same function rather than a cause. Its FuncInfo has nTryBlocks = 0 and its unwind actions live at 0x679110-0x679128, so no funclet was ever involved. Both EH readings (C122) were tested and refuted before this one was found.

## The fix

Two places, and both are needed:

  * **recomp.py / the runtime** must treat an indirect JMP through a table as an intra-function branch, not as a dispatch to another function. That is the actual defect.
  * **native_discover.sh must never seed a jump-table target.** Collect every `JMP dword ptr [reg*4 + <imm>]` in the module, read its table, and refuse any reported address that appears in one -- with the table's address named, so the refusal explains itself.

STOP seeding in this region until the first is done.

### Resolution (2026-08-06)
ROOT CAUSE FOUND (C123): the reported 'missing indirect call targets' are SWITCH CASE LABELS from jump tables at 0x005fb240 and 0x005fb250, and the recompiler dispatches the indirect JMP through them as if it were a call. Not a boundary problem, not exception handling. The fix is in recomp.py plus a jump-table guard in native_discover.sh; see the final note for both. Resolved as diagnosed, not as fixed.

### Note (2026-08-06)
CORRECTION to the fix, and it is cheaper than stated above.

**recomp.py already handles this correctly.** `fn['_has_injmp']` is set when a function contains an indirect JMP; it then emits a C label for EVERY instruction address in the function, plus an `L_injmp` switch mapping module-relative offsets to those labels, falling through to the global dispatcher only for a genuine tail call out of the module. A whole function with a switch resolves its own case labels internally and never reaches the dispatcher at all.

So the translator is NOT the defect, and the note above was wrong to say it must be changed. The defect is that the function is not whole: once a case label is seeded and Ghidra carves the function, that label is no longer in `fn['_addrs']`, the `L_injmp` switch has no case for it, and it falls through to DISPATCH -- reporting 'no recompiled body'. **The carve manufactures the very symptom that prompts the next carve.** That is the feedback loop, stated exactly.

**The fix is therefore one place, not two**: native_discover.sh must refuse to seed any address that appears in a jump table, and the damage already done in this region has to be undone so 0x005fac10 is one function again.

One practical warning for whoever writes the guard: the tables at 0x005fb240 and 0x005fb250 are ADJACENT, so reading a fixed number of entries from the first runs straight into the second. Take each table's length from the switch's own bound check -- the `JA` immediately above the `JMP` -- rather than assuming one.

### Note (2026-08-06)
TRANSLATOR FIXED AND TESTED; ONE THING LEFT, AND IT IS IN THE DATABASE.

The `_has_injmp` gate is fixed (commit 'Test the translator, and fix the switch dispatch it never covered'): it gated on the exporter's `ind` flag, which Ghidra does not set on `JMP dword ptr [reg*4 + <table>]`, so the local dispatcher was never generated for ANY switch in the image. tests/test_recomp.py pins it -- 4 of 6 cases fail on the old predicate, naming the emitted `DISPATCH(C, RD32(...))` where `goto L_injmp;` belonged.

Re-lifted with the fix, and the run is UNCHANGED: still 'no recompiled body at 0x005facd5'. The reason is separate and now measured:

    FUN_005fac10: 426 ins, spanning 0x005fac10..0x005fb23c
       contains 0x005facd5: False
       contains 0x005face5: False
       contains 0x005facf6: False
       contains 0x005fad07: False
    0x005facd5 belongs to: NO FUNCTION

**The function body has HOLES exactly where its own switch cases are.** Those addresses sit inside its address range and belong to no function, so the translator has no instruction to label and `L_injmp` cannot have a case for them. The fix is correct and cannot help until the body is whole.

This is residue from the seeding and carving: the blocks were pulled out into split-off functions, and `--merge` (which absorbs inner FUNCTIONS) cannot reabsorb them now that they are not functions at all.

**Next, and it is a Ghidra-database operation, not a code change**: make FUN_005fac10's body cover its whole range. Deleting the function and re-creating it at 0x005fac10 lets Ghidra re-walk the flow including the jump tables; `--reanalyze` may do it. Verify with the snippet above -- 'contains 0x005facd5: True' is the gate -- and only then re-run. Do not seed.
