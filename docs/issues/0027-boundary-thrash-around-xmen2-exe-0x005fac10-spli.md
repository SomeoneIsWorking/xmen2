---
id: 27
title: Boundary thrash around XMen2.exe 0x005fac10: split, merge and seed each move the failure without fixing it
status: resolved
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

### Note (2026-08-06)
PARTLY RESOLVED, and the boundary work was the wrong lever throughout.

The abort that made this region look broken was a FALSE POSITIVE: x86_return_to aborted on a tail-called body's RET, which mismatches its entry [esp] by construction. Fixed (see the commit 'A tail-called body's RET is not corruption'). With that, the region needs NO carving: seeding the two functions the runtime genuinely could not find (0x005fad31, 0x005fb270) is enough, and the discovery loop then converged in 3 rounds instead of thrashing.

STILL OPEN, and it is a different problem in a different place: XMen2.exe 0x0066cf4e is a SWITCH. --recreate wired its jump table (10 entries, table at 0x0066d645) and grew the function from 1 to 501 instructions. One case label, 0x0066cf79, is also a detected function whose body ends with no terminator and falls through to 0x0066cf7d. --merge REFUSES to absorb it ('the inner function is real'), so the case label cannot currently be un-made through the existing tools.

That is the next thing to fix, and it is the same class C124 and Xbox issue #6 record: a switch case label seeded as a function. The tool gap is that MergeTruncated has no way to be told 'this one is a case label, absorb it anyway'.

### Resolution (2026-08-06)
The tool gap is closed at its cause: RecreateFunction.py now UN-MAKES a function that sits on one of the container's own jump-table entries, so the flow walk can absorb the case.

WHY THE GAP EXISTED. Wiring the table gives the flow walk its targets, but Ghidra will not absorb another FUNCTION's entry point, so a case label seeded as a function keeps its block -- and the whole fall-through chain behind it -- outside the container permanently. --merge was the wrong lever throughout: it refuses (correctly) when the inner function looks real, and it reasons about the function AFTER the truncated one, not about the jump table.

THE DISCRIMINATOR, and why it is not a force flag: the address is not guessed to be a case label, it was READ OUT of the container's own jump table four bytes at a time. What is weighed is only whether something ELSE treats it as an entry point -- a CALL reference (the container is then legitimately non-contiguous; refused) or a non-default name (a human attached something; refused). The rule lives in tools/ghidra_scripts/caselabel.py, free of any ghidra import, and is tested in tests/test_caselabel.py (16 cases, ctest 'caselabel'), because the action it authorises is DELETING a function object.

ALSO FIXED HERE: RECREATE_EXPECT certified 'inside SOME function', which cannot fail for this defect -- a case label seeded as a function is already inside a function, itself. It now requires the address to be inside a function THIS RUN re-created.

MEASURED on XMen2.exe 0x0066cf4e: entry 0 of the table at 0x0066d645 was FUN_0066cf79 (one 'CMP dword ptr [EBP + 0x8],0x3', no terminator). Un-made -> the body went 501 -> 577 instructions and BOTH holes closed (0x0066cf55..0x0066cfd3, 127 bytes, and 0x0066cfdb..0x0066d042, 104 bytes); 0 holes remain, 16114 -> 16113 functions.

WHERE THE RUN GOES NOW: it clears 0x0066cf4e entirely, creates the D3D8 device (800x600) and a Vulkan swapchain, and stops on an honest work item -- IDirect3DDevice8::SetPixelShader is not implemented by the host D3D8. --d3d8-permissive walks further to a missing body at XMen2.exe 0x0066f298, ordinary discovery-loop input, but that is past a FAKED SetPixelShader and so is not trustworthy as a frontier.
