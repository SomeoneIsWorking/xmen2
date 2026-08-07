---
id: 36
title: The native run faults at NULL in FUN_005beca0 during scene traversal, past the frame limiter
status: open
symptom: x2native --d3d8 --run: SIGSEGV at (nil), addr2line names fn_XMen2_005beca0; one frame presented, then ~5s of work and the fault
tags: pc,native,rc-exe,graphics,scenegraph
created: 2026-08-07
updated: 2026-08-07
---

## Where the run gets to

Everything issue #35 unblocked: module init, CRT, engine memory, ARK, renderer
init with a live Vulkan device, input, **one presented frame**, and then a long
single-frame stretch of scene work (`igGraphPath`, `igCamera::activate`,
`igTObjectList<igAttrSet>::get`, `igObject::ref`/`release`) before the fault.

Not a missing body: `tools/native_discover.sh` converged in three rounds on
this exact path, so nothing between the start and this point is a function
static analysis missed.

## The fault

    *** SIGSEGV at (nil) (not an import slot)
    [REGS] eax 00086754  ecx 710d9bb4  edx 00000000  ebx 3fffffff
    [REGS] esp 700ffa14  ebp 00000000  esi 71053460  edi 000002ad

    addr2line -fCe scratch/build-native/x2native <host rip>  ->  fn_XMen2_005beca0

The instruction is one of

    005becb0  MOV EDX,dword ptr [EAX + ESI*0x1]
    005becb8  CALL dword ptr [EDX]              ; EDX = 0 in the register dump

so it is an indirect call through a vtable pointer read from `[EAX + ESI]`,
where that slot holds NULL. `ebx 3fffffff` is worth a look: it is a mask, not a
pointer, and it is the shape a bad shift or a bad AND produces.

## Where to start

1. `X2_ARGS=0x005beca0` on a trace build gives ECX, the four stack words, and
   the RETURN ADDRESS of every call, capped by novelty -- so the caller and the
   object it was handed are one run away.
2. `X2_PEEK` on `EAX + ESI` at that moment says whether the slot was never
   written or was overwritten.
3. The heartbeat (`X2_HEARTBEAT`) shows the run presents exactly ONE frame and
   then works for seconds without presenting, which is what a level load looks
   like -- so the object is most likely something the load path builds.
