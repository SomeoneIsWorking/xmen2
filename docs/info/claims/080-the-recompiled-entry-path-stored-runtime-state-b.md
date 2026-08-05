---
id: C080
kind: claim
status: holds
created: 2026-08-05
tags: pc,recomp,libigdisplay,stack,abi,root-cause
---

## Claim

The recompiled entry path stored runtime state BELOW the guest stack pointer, so the guest and every host function it called overwrote it. Fixing that -- a private per-thread runtime stack, and a shim that pushes nothing that must outlive the body -- makes the FULL 521-function hybrid libIGDisplay.dll run the game to the intro.

## Evidence

MEASURED, not read off the code. (1) src/x86watch.c x86_watch_stack printed 'SHARED STACK: the CPU struct ends 36 bytes BELOW guest_esp'. (2) The new crash reporter src/x86fault.c named the fault: EIP 0x00b7fc60 == the CPU struct's own base address; libIGCore's igArkRegister (libIGCore.dll+0x45180, 'sub esp,0x108') overlapped 196 of the struct's 232 bytes. (3) After giving the runtime a private stack, the same build's watch shows both bodies RETURNED where before neither did; the second half (the shim's return address 20 bytes below the entry ESP, overwritten by the last host callee) produced a RET into the private stack, fixed by x86_enter_tramp. (4) tools/build_recomp.sh ALL + tools/run_shim.sh all: 2307-colour rendered frames, 17 recompiled entries over 9 distinct entry points, ZERO fault blocks -- where the same set previously page-faulted before rendering.

## What would falsify it

if a run of the ALL build ever shows a [FAULT] block whose EIP or overwritten target lies inside the private runtime stack region, the separation is not complete; and if X2_WATCH=all ever reports zero ENTER lines on a passing ALL build, the pass is C079 again (nothing ran) rather than this fix
