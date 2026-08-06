---
id: 29
title: The native run's frontier: a direct JMP into the INTERIOR of another function has nowhere to jump to
status: open
symptom: x86_call_unknown: 0x0066cf3c has no identified function -- 'direct call to an address with no identified function', where the address is inside a function but is not its entry
tags: pc,recomp,native,rc-exe,rc-lift,translator
created: 2026-08-06
updated: 2026-08-06
---

## Where the run stops now

With the switch repairs of issue #27 and C132/C133 in, the `--d3d8 --run` path clears module init, the CRT, engine memory, ARK, renderer init (D3D8 device + Vulkan swapchain), `SetPixelShader`, `SetGammaRamp` and the longjmp, and stops at:

    x86_call_unknown: 0x0066cf3c has no identified function

## What 0x0066cf3c is

The shared tail of a function:

    0066cf3c  PUSH ESI / PUSH EBX / CALL 0x0066fcf7 / ADD ESP,0xc
    0066cf46  POP EDI / POP ESI / POP EBX / LEAVE / RET

It is INSIDE `FUN_0066ced2` (which falls through into it), and `FUN_0066cf4e` -- the switch -- reaches it with `JMP 0x0066cf3c` at 0x0066d633. MSVC shares one epilogue between the paths.

**This is not a boundary defect.** `FUN_0066ced2` ends at 0x0066cf4a with a `RET`: it is complete, not truncated, and `--merge` now says so and refuses. Carving 0x0066cf3c out would truncate its predecessor instead.

## Why the translator cannot emit it

`recomp.py` translates a direct `JMP` whose target is outside the current function as a tail call to that function -- `fn_<target>(C)`. There is no name for an address that is not an entry point, so it emits `x86_call_unknown` (honestly: it says exactly what it could not do).

## The size of it, measured

Across XMen2.exe: **28 distinct interior JMP targets from 38 sites, and 0 interior CALL targets.** Several land in the same container (5 in `FUN_00593a58` alone), which is what a shared epilogue looks like. So this is a pattern, not a one-off, and splitting all 28 would carve 28 blocks out of ~10 functions and truncate each predecessor.

## The mechanism it needs

Enter a generated body at an interior label. The machinery is already there for INDIRECT jumps: a function with a computed jump emits `L_injmp` and a `switch (target - G_IMGBASE) { case ...: goto L_<addr>; }` over every address in its body. What is missing is a way in from outside:

- a CPU field (`enter_at`) that a body checks in its prologue, jumping through the existing `L_injmp` switch;
- direct `JMP <interior>` emitting `{ C->enter_at = <target>; fn_<owner>(C); return; }`;
- the dispatch table gaining an entry per interior target, so an INDIRECT dispatch to one resolves too -- which is what would have made 0x005fafc1 work without any boundary surgery at all.

That last point matters: several sessions of splitting and merging around 0x005fac10 were chasing addresses this mechanism would simply have handled.

## Do NOT

Split these 28 addresses out into functions. It carves a real body, truncates the predecessor, and has to be undone -- see issues #21 and #27 for what that costs.
