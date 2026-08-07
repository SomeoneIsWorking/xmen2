---
id: 36
title: The native run faults at NULL in FUN_005beca0 during scene traversal, past the frame limiter
status: resolved
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

## Root cause: the entry point was not the lowest address in the body

`recomp.py` emitted a function's instructions in address order and fell into the
first one, which assumes the entry point IS the lowest address. MSVC puts an
adjustor thunk far from the code it jumps to, and Ghidra merges the two ranges
into one function whose ENTRY is the higher address:

    005bee90  MOV EDX,dword ptr [ESP + 0x4]     <- lowest address, NOT the entry
    005bee94  SUB EDX,ECX
    ...       (signed divide by 0x324 -- the MSVC magic-multiply form)
    005beeab  JMP 0x005beca0
    005d4d80  ADD ECX,0x867ac                   <- the entry point
    005d4d86  JMP 0x005bee90

So `FUN_005d4d80` ran from `0x005bee90` and its own first instruction was
emitted after a `return`. `ADD ECX,0x867ac` never happened, the array base was
never applied, and `(ptr - base) / 804` came out as **685 instead of 0**: the
fault is on element 685 of a 175-element array, two functions and a vtable
dispatch away from the instruction that was skipped. It links, it runs, and it
is wrong -- the exact failure mode the translator's rules exist to prevent.

12 functions across 3 modules have an entry point that is not their lowest
address. All of them have an instruction AT the entry point, so all 12 were
running from the wrong place.

**Fix**: the body jumps to its entry point's label when the two differ, and
`translate()` refuses a function whose instruction list does not contain its own
entry point at all. Four cases in `tests/test_recomp.py`, including that
refusal and the negative (an ordinary body must emit no jump).

**After the fix** the NULL dereference is gone and the run stops on a NAMED
missing feature: `IDirect3DDevice8::CreateStateBlock` (slot 57). With
`--d3d8-permissive` it walks past that and asks for `FindFirstFileA` -- the
asset scanner. Both are ordinary work items, not defects.

## How it was found (the two instruments, again)

The argument watch said `FUN_005beca0` was called ONCE with `0x2ad`, while
`X2_PEEK` on the slot the caller reads showed `0x710d9c0c` **at the same
instant**. Two instruments contradicting each other on one address is not a
tie -- it means a third thing is true. The boundary ring, which now records the
caller, showed what:

    enter FUN_005d8920  <- 0x005af601
    exit  FUN_005d8920
    enter FUN_005d4d80  <- 0x005af613      <- the vtable call went HERE
    enter FUN_005beca0  <- 0x005af613      <- which TAIL-CALLED here

There was a function in between, tail-calling with a rewritten argument. Neither
watch was lying; the assumption that the caller was the one computing `0x2ad`
was wrong.

Also added: the argument watch prints the callee-saved four (`ebx/ebp/esi/edi`)
at entry AND at exit, which is what ruled out register corruption in one run.

## Original notes: where to start

1. `X2_ARGS=0x005beca0` on a trace build gives ECX, the four stack words, and
   the RETURN ADDRESS of every call, capped by novelty -- so the caller and the
   object it was handed are one run away.
2. `X2_PEEK` on `EAX + ESI` at that moment says whether the slot was never
   written or was overwritten.
3. The heartbeat (`X2_HEARTBEAT`) shows the run presents exactly ONE frame and
   then works for seconds without presenting, which is what a level load looks
   like -- so the object is most likely something the load path builds.
