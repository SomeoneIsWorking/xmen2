---
id: 27
title: Boundary thrash around XMen2.exe 0x005fac10: split, merge and seed each move the failure without fixing it
status: dead-end
symptom: x86_return_to: 0x005fb2bc is not a function entry. The RET is in FUN_005fafc1, entered with 0x000c0260 on the stack. Splitting, merging and re-seeding move the symptom between 0x005fafc1, 0x005fad31 and 0x0066cf79 without resolving it.
tags: pc,recomp,rc-exe,function-boundaries,dead-end,discovery-loop
created: 2026-08-06
updated: 2026-08-06
---

## What was tried, in order, and what each did

1. `native_discover.sh` on the `--d3d8` path seeded 0x005fafc1 and ESCALATED it
   to a split, because it falls inside 0x005fac10. That CARVED a real function:
   0x005fafc1 begins `LEA EDI,[ESI + 0x28]`, with ESI holding a live object
   from the caller, which no function entry does.
2. `--recreate 0x005fafc1` rebuilt the container 0x005fac10 from control flow
   (+51 instructions) but left the carved function in place. Same failure.
3. `--merge 0x005fafc1` absorbed it back. The failure MOVED: a direct call to
   0x005fad31 -- which is a REAL function, and is the very address issue #21 is
   about -- now had no body, because the merge had swallowed it too.
4. Seeding 0x005fad31 restored that function, and Ghidra's analysis recreated
   0x005fafc1 along with it. Back to the failure in step 1.

That is a loop, and it is the same loop issue #21 records. Recording it as a
DEAD END so the next session does not walk it again.

## What is actually known

- 0x005fafc1 is dispatched to at runtime as a call target. Something computes
  an INTERIOR address of 0x005fac10 and calls it.
- When it is entered, `[esp]` holds 0x000c0260 -- not a code address, and not
  in any mapped module. So whatever called it did so with a stack that was
  already wrong.

Those two together say this is probably NOT a boundary problem at all. A
function whose caller arrives with a garbage return address is a symptom of
the CALLER, and carving the callee to match cannot fix it. The PUSH/POP defect
fixed this session was exactly that shape -- a stack read one dword off -- and
was found by dumping the guest stack at the failure rather than by adjusting
boundaries.

## What to do instead

Find WHO dispatches to 0x005fafc1 and what its stack looks like at that
moment. `x86_dispatch` names the dispatching body now, and the malloc guard
shows the pattern to copy: dump the actual guest stack at the failure instead
of reconstructing it from the boundary ring, which can be showing a different
invocation of the same function.

Do NOT split, merge or seed anything in 0x005fac10..0x005fb2bc until that
question is answered.

## Tool change made because of this

`tools/whose_function.py` now refuses a split whose candidate address does not
begin with an instruction a function plausibly starts with, not only one whose
container has an SEH prologue. The SEH check alone passed 0x005fafc1.
