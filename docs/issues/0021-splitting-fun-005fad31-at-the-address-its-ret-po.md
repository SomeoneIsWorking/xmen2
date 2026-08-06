---
id: 21
title: Splitting FUN_005fad31 at the address its RET popped makes a bogus function, it does not fix the stack imbalance
status: dead-end
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
