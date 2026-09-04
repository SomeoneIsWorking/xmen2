---
id: 20
title: Native --vk run SIGSEGVs at NULL inside igDxVisualContext::releaseVolatileResources / userRelease
status: resolved
symptom: SIGSEGV at (nil) with ecx=0 during the --vk run's teardown; addr2line names fn_libIGGfx_1002b7b0 releaseVolatileResources. The engine's userRelease does MOV ECX,[ESI+0x53c]; MOV EDX,[ECX]; CALL [EDX+0x58] on a NULL shader manager.
tags: pc,native,graphics,vulkan,ark,pe
created: 2026-08-06
updated: 2026-08-06
---

## What happens

Running `x2native --no-window --vk --run`, the renderer substitution installs,
the engine creates a real Vulkan device (C120), the construction helpers run,
`setVideoMode` is accepted -- and then the engine tears the context down and
SIGSEGVs at NULL inside its own teardown.

## Cause, named

**The ARK class inherits the WRONG constructor.** `igVkVisualContext`
registers with `igVisualContext`'s parent hooks, so libIGCore runs
igVisualContext's construction. `igDxVisualContext`'s own constructor -- the
thing that builds the capability manager at `this+0x534` and the shader
manager at `this+0x53c` -- never runs. Those two fields stay zero, and the
teardown dereferences `this+0x53c` unguarded.

This is recorded as C121 with the field-by-field evidence. A field report was
added to slot 7 (`report_fields` in `src/vulkan/igvk_slots_lifecycle.c`) that
prints the twelve fields construction is supposed to produce; on a real run it
shows `+0x534 = 0` and `+0x53c = 0` while everything the hand-written helper
list does produce is non-zero. That report is the diagnosis, printed where it
becomes true rather than at the crash 300 instructions later.

## The fix, not yet applied

Register `igVkVisualContext` as a subclass of **igDx8VisualContext** rather
than of igVisualContext, so the whole Dx constructor chain runs and only the
vtable is ours. `tools/ark_classes.py` gives igDx8VisualContext's registrar
(linked 0x10014a60) and its `getClassMetaSafe` (linked 0x10009880).

**The open question that must be answered first**: the value a child passes as
`parentGetClassMeta` is NOT the parent's `getClassMetaSafe`. For child
igDxVisualContext (parent igVisualContext) it is 0x1004afc0 while
igVisualContext's own safe form is 0x100038c0; for child igDx8VisualContext
(parent igDxVisualContext) it is 0x100077f0 while igDxVisualContext's safe
form is 0x10007800. So there is a second, non-"safe" getClassMeta per class,
and igDx8VisualContext's has to be located before this can be done. Guessing
it would register the class against the wrong parent, which fails far away
from the mistake.

Also worth checking at the same time: whether ARK accepts a class whose parent
is CONCRETE. Every existing example inherits from an abstract parent.

## Related, fixed on the way

The hand-written substitute for `igDxVisualContext::userInstantiate` had
silently dropped three of the real body's calls (0x10094490 twice, and
0x1002d230) and allocated the three parameter blocks at a generous 0x200 each
where the engine allocates 0x34 / 0x34 / 0xd4. Both corrected against the
disassembly. The 0xd4 block is the D3DCAPS8 that `IDirect3D8::GetDeviceCaps`
fills -- which independently confirms C108's finding that this build uses the
PC D3D8 vtable layout -- and it is currently left ZEROED, so every capability
the game queries reads "not supported".

### Note (2026-08-06)
FIXED. The class now registers as a subclass of igDx8VisualContext (parent hooks: registrar linked 0x10014a60, getClassMeta linked 0x10009870), so libIGCore runs the whole Dx constructor chain and only the vtable is ours.

The blocking question in this issue -- that a child passes the parent's NON-safe getClassMeta, and igDx8VisualContext's was unknown -- was settled by SEARCH, not by guessing the pattern. Every getClassMeta in libIGGfx is exactly two instructions, `MOV EAX,[<the class's meta slot>]; RET`. Searching for that body reading igDx8VisualContext's meta slot 0x10189450 found 0x10009870 uniquely, and it is also vtable slot 20 of igDx8VisualContext, which confirms it independently.

The other open question is answered too: **ARK does accept a class whose parent is concrete.** Registration succeeded and all three chain rebinds took.

Verified on a real `--vk` run: the slot-7 field report goes from 5 zeros to 3
(the two NULL-BY-DESIGN device pointers plus `this+0x14`), with +0x534 =
0x046725c8 and +0x53c = 0x04672600 now built. The SIGSEGV is gone and execution
proceeds past teardown into XMen2.exe.

### Resolution (2026-08-06)
Registered as a subclass of igDx8VisualContext instead of igVisualContext, so the whole Dx constructor chain runs; +0x534 and +0x53c are built and the SIGSEGV is gone. See the note above for how the blocking getClassMeta question was settled.
