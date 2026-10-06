---
id: C121
kind: claim
status: holds
created: 2026-08-06
tags: graphics,vulkan,ark
reconfirmed: 2026-08-06
---

## Claim

Hand-assembling igDxVisualContext's construction is the wrong layer: the ARK class inherits igVisualContext's constructor, so the Dx subclass's own fields are never built

## Evidence

igVkVisualContext registers with igVisualContext's parent hooks (arkRegisterInternal 0x1000b440, getClassMeta 0x1004afc0), so libIGCore runs igVisualContext's construction and NOT igDxVisualContext's. The Dx constructor at libIGGfx 0x100082d1 is the only writer of this+0x330 besides initTexture, and it is also what builds the capability manager at this+0x534 and the shader manager at this+0x53c. A field report added to slot 7 shows, on a real --vk run: +0x534 = 0 and +0x53c = 0 while +0x18/+0x178/+0x330/+0x35c are all non-zero. igDxVisualContext::userRelease then does MOV ECX,[ESI+0x53c]; MOV EDX,[ECX]; CALL [EDX+0x58] and takes SIGSEGV at NULL with ecx=0 -- observed, addr2line naming fn_libIGGfx_1002b7b0 releaseVolatileResources on the same path. Separately, the real userInstantiate at 0x1002c210 is 398 instructions and the hand-written substitute had silently dropped three of its calls (0x10094490 twice and 0x1002d230) and over-allocated the three parameter blocks at 0x200 each where the engine allocates 0x34/0x34/0xd4 -- the 0xd4 one being D3DCAPS8, which independently confirms C108's PC-vtable finding.

## What would falsify it

Registering igVkVisualContext with igDx8VisualContext's parent hooks instead, and finding +0x534/+0x53c still zero -- which would show those fields are set by something other than the constructor chain. Also falsified if ARK refuses a class whose parent is concrete.

## Re-confirmed 2026-08-06

FIX APPLIED AND VERIFIED. igVkVisualContext now registers with igDx8VisualContext's parent hooks -- registrar linked 0x10014a60, getClassMeta linked 0x10009870 -- instead of igVisualContext's. The blocking question (a child passes the parent's NON-safe getClassMeta, and igDx8VisualContext's was unknown) was settled by search rather than guess: every getClassMeta in libIGGfx is literally two instructions, MOV EAX,[<the class's meta slot>]; RET, so searching libIGGfx for that exact body reading igDx8VisualContext's meta slot 0x10189450 found 0x10009870 uniquely. It is also vtable slot 20 of igDx8VisualContext, an independent confirmation. ARK accepts a CONCRETE parent: registration succeeded and all three chain rebinds took. On a real --vk run the field report goes from 5 zeros to 3, with +0x534 = 0x046725c8 and +0x53c = 0x04672600 now built, and the SIGSEGV at NULL in the engine's teardown is gone.
