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
