---
id: 23
title: Native --vk: stack imbalance in libIGGfx 0x1002ead0, reached from open()'s second helper
status: open
symptom: x86_return_to: 0x000001c0 is not a function entry. The RET is in 0x1002ead0, entered with 0x2502ca42 on the stack and left with 0x000001c0 there. Reached during igVkVisualContext::open, after the render destination slots were implemented.
tags: pc,recomp,native,graphics,vulkan,rc-lift,rc-exe
created: 2026-08-06
updated: 2026-08-06
---

## Where it sits

This is what stops the `--vk` run today. Everything before it now works: the
renderer is substituted, a Vulkan device is created by the engine's own code,
display initialisation succeeds, the swapchain is claimed on the guest's
window, the Cg loader is handled, and the engine has driven slots 38, 30, 25,
47 and 50.

`igvk_frame_bind_target` reports that the engine bound **render destination
3** — an off-screen target this backend does not have — immediately before
this, which is a separate known gap but not obviously the cause.

## The lead

It was ENTERED with `0x2502ca42` on the stack. That is a mapped libIGGfx
address, and `0x1002ca42` sits just inside `0x1002ca30` — which is
`DX_OPEN_HELPER_B`, the second of the two helpers `vk_open` calls while
transcribing `igDxVisualContext::open`. So the caller is almost certainly that
helper, invoked from our slot 30.

Two readings, and they need different fixes:

1. **A recompiler defect in 0x1002ead0** — its detected boundaries are wrong
   or an instruction is mistranslated. Same class as issue #21, which turned
   out to be a switch whose case labels had been carved out. Check first
   whether 0x1002ead0 contains an indirect `JMP` through a table and whether
   its body has holes; `tools/whose_function.py` and
   `RecreateFunction.py --recreate` are the tools.
2. **`vk_open` calling a helper it should not** — `DX_OPEN_HELPER_B` is called
   because the engine's own `open` calls it, but so was
   `checkAndCreateSurfaces`, and that one turned out to be unguarded device
   work. If 0x1002ead0 reaches the device, the helper needs the same treatment.

Reading (1) is cheap to test and should go first: it is a yes/no about the
function's body, not a design question.

## What must not be done

Do not seed or split `0x1002ead0` on the strength of the address in the
message. That is exactly the loop issue #21 documents.

### Note (2026-08-06)
READING (1) REFUTED, AND THE FAULT IS OURS.

0x1002ead0 is `Gap::Gfx::igDxVisualContext::setupDrawing`, and it is a clean function: 28 instructions, 0x1002ead0..0x1002eb25, **no indirect JMP, no holes in its body, one plain RET**. So the boundary/switch reading -- the issue #21 shape -- does not apply here. Checked before doing anything else, as the note above said to.

Its whole body is two vtable calls on ITSELF:

    1002ead3  MOV EAX,[ESI+0x17c]        ; current render destination
    1002ead9  MOV ECX,[ESI+0x184]
    1002eae1  JZ  0x1002eaf0             ; equal -> skip
    1002eae5  PUSH 0x0
    1002eae7  PUSH EAX
    1002eaea  CALL dword ptr [EDX + 0xbc]   ; slot 47  setRenderDestination
    ...
    1002eb1e  CALL dword ptr [EAX + 0x2e8]  ; slot 186 setViewport (6 args)
    1002eb25  RET

**Both of those are slots this backend implements**, and both were implemented in the commit that produced this symptom. So the guest stack is being shifted by one of our own stubs, not by a mistranslation: setupDrawing's RET then pops whatever is left.

Reading (2) in the note above is therefore also wrong as stated -- it is not DX_OPEN_HELPER_B reaching the device. The caller identification stands (0x2502ca42 is inside 0x1002ca30) but the DAMAGE is in the callees.

**Where to look, in order:**

1. `vk_set_render_destination` pops 2 args on every path (`ark_ret(C, 0, 2)`), matching RET 8. Verify every early return does too -- there are three.
2. `vk_set_viewport` pops 6, matching RET 0x18.
3. **The most suspicious thing, and it is new in the same commit**: `vk_set_render_destination` re-enters the object's OWN vtable, `ark_call_this(RD32(vt + 186*4), self, vp, 6)`, to make the trailing setViewport. That dispatches a SYNTHETIC stub address through x86_guest_call on the scratch stack. If that path does not restore the scratch stack pointer the way a real body would, or if the synthetic address does not route to the native stub, the corruption starts there. Test it by having slot 47 call igvk_frame_viewport directly instead of re-entering the vtable, and see whether the imbalance moves.

That third one is a design choice I made deliberately (so an override of slot 186 would be honoured) and it is the first thing to suspect precisely because it is the unusual part.
