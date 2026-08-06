---
id: 22
title: Native --vk run: the game's own 'Display failed!' dialog -- the renderer does not report itself as initialised
status: open
symptom: MessageBox [Display failed!] 'Unable to initialise graphic display. Resolution and FSAA have been reverted to default.' on the --vk run, after igVkVisualContext constructs, creates a real Vulkan device and accepts setVideoMode. Followed by a SIGSEGV in Gap::Core::igArenaMemoryPool::consolidate during the teardown that follows.
tags: pc,recomp,native,graphics,vulkan,rc-exe
created: 2026-08-06
updated: 2026-08-06
---

## Where this sits

This is the FIRST renderer-side failure reached on its own merits. Everything
before it is now working: the ARK substitution installs, the engine's own
constructor chain runs, a real Vulkan device is created, the construction
helpers run and `setVideoMode` is accepted. The recompiler defect that used to
stop the run earlier is fixed (C124).

`0 frame(s) presented` — the frame path is never driven, because the engine
concludes the display did not come up and unwinds instead.

## What is NOT the cause

**No unimplemented slot was dispatched.** The vtable's unimplemented reporter
aborts by name and index, and it never fired. So the engine did not ask for
`open` (slot 30), or any other slot we owe, before deciding this. Whatever
made the decision did so from state, not from a call we refused.

That rules out the obvious first guess and is the useful thing this entry
records.

## What to do next

Find who raises the dialog and what it tested. `MessageBoxA` is already
intercepted by the host — put the caller's return address in that report, and
the deciding function names itself in one run. Do that BEFORE implementing any
more slots: implementing `open` on the assumption that it is the blocker would
be guessing, and the evidence above says it is not being called.

Likely candidates once the caller is known, in the order they are cheap to
check: `this+0x148` is an allocated but ZEROED `D3DCAPS8`, so every capability
query answers "not supported" and any check for a required capability fails;
`initDesktopDisplayFormat` and `initCg` are still not called; the two virtual
calls through `this+0x534` and `this+0x53c` that the engine's own
`userInstantiate` makes are still not made.

## The secondary fault

The SIGSEGV in `igArenaMemoryPool::consolidate` happens during the unwind
AFTER the dialog, and it is issue #15's function recurring. It is downstream
of the failure, not its cause — fix the display path first and re-check
whether this still reproduces.

### Note (2026-08-06)
THE DECIDING FUNCTION IS NAMED, and the trail is three hops with no guessing left in it.

MessageBoxA now reports its caller (src/native/win32_sdl.c) -- the text alone said what the game concluded, never which check concluded it:

    *** MessageBox [Display failed!]
        raised from 0x004035a7

**Hop 1 — FUN_00403420 at 0x004035a7.** The dialog is gated on a flag, not on anything it computes itself:

        0040356b  MOV AL,[0x00a09f94]
        00403570  TEST AL,AL
        00403573  JZ  0x004035b3          ; zero -> skip the dialog entirely
        00403575  MOV AL,[0x006f3c2c]
        0040357c  JNZ 0x004035a7          ; non-zero -> also skip it
        ...       MessageBoxA

So the dialog appears exactly when `0x00a09f94 != 0` and `0x006f3c2c == 0`.

**Hop 2 — who sets 0x00a09f94.** Exactly one function, twice:

    FUN_005fb270  0x005fb294  MOV byte ptr [0x00a09f94],0x1
    FUN_005fb270  0x005fb320  MOV byte ptr [0x00a09f94],0x1

(FUN_005fb270 is the function that follows the jump tables — the one whose absence used to stop the run.)

**Hop 3 — what FUN_005fb270 tests.** Its first store is guarded by one byte:

        005fb270  MOV EAX,FS:[0x0]        ; its own SEH frame
        005fb27e  MOV AL,[0x006f3a2d]
        005fb28d  TEST AL,AL
        005fb292  JZ  0x005fb2ab          ; zero -> carry on
        005fb294  MOV byte ptr [0x00a09f94],0x1   ; non-zero -> FAIL

So **`0x006f3a2d` is the thing to chase**: something sets it before this runs, and that is the real display-initialisation failure. The second store at 0x005fb320 is on a different path in the same function and still has to be read.

**NEXT, and it is a grep not a guess**: find the writers of `0x006f3a2d` the same way. Do not implement renderer slots on a hunch until that byte's setter is named -- the evidence still says no slot we owe is being dispatched before this.

### Note (2026-08-06)
HOP 4, and it changes the method: 0x006f3a2d is an ERROR LATCH, not a specific cause. Nine functions write it, all storing 1:

    FUN_00403420 0x004034e7   FUN_005c9640 0x005c96dd   FUN_005faa20 0x005fab70
    FUN_005fac10 0x005faf4f   FUN_005fac10 0x005fb194   FUN_00617480 0x006175c9
    FUN_0061c3b0 0x0061c4b6   FUN_0061f9c0 0x0061fa86   FUN_006223d0 0x006223e7

One of those is FUN_00617480 -- the DirectX 9.0c presence check this port already RETIRES via a native override (issue #18). So the latch is the game's generic 'something in startup failed' byte, and reading further up the static call graph cannot say which of the nine fired.

**So stop grepping and watch the byte.** What is needed is a write-watch on 0x006f3a2d that reports the FIRST writer with its address, the same shape as the existing entry-point watch (X2_WATCH). Two of the nine are inside FUN_005fac10, the display-init function itself, which makes them the likely candidates -- but 'likely' is exactly what an instrument is for, and guessing between two branches of a 477-instruction function is how the earlier detours in issue #21 started.

Everything up to here is settled: the dialog's caller, its two gating globals, the single function that sets 0x00a09f94, and the byte that function tests. Only the identity of the writer is open, and it is a one-instrument question rather than an analysis one.
