---
id: 22
title: Native --vk run: the game's own 'Display failed!' dialog -- the renderer does not report itself as initialised
status: resolved
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

### Note (2026-08-06)
HOP 5 — THE WRITER IS NAMED, by instrument rather than by reading. Built with -DX2_NATIVE_TRACE=ON (which is what compiles in the X2_ARGS entry watch) and watched all eight candidate functions at once:

    [ARGS] watching 8 entry point(s)
    [ARGS] -> 0x00403420 FUN_00403420   ...  (ret to 00672779)
    [ARGS] -> 0x005fac10 FUN_005fac10   ecx 00a0a138  (ret to 005fb2bc)
    [ARGS] <- 0x005fac10  eax 700ffecc
    *** MessageBox [Display failed!]
    [ARGS] 2 call(s) reported across 8 watched entry point(s)

**Only two of the eight ran at all**, and FUN_00403420 is the function that shows the dialog, not a setter of the latch on this path. So **FUN_005fac10 set `0x006f3a2d`** — the display-init function, the same 477-instruction switch this whole saga has been about. It returns to 0x005fb2bc, inside FUN_005fb270, which then reads the latch and raises the flag. The chain is closed end to end with no inference left in it.

Note the negative is as informative as the positive: FUN_00617480 (the retired DirectX check) did NOT run, so the override is not implicated, and neither did the other five.

**REMAINING QUESTION, now narrow**: which of FUN_005fac10's two stores fired — 0x005faf4f or 0x005fb194 — and what did it test. Both are inside one named function whose body is now complete and correct, so this is a read of two branch conditions, not a search. Watch it with X2_ARGS or read the guards directly.

**Build note**: scratch/build-native is currently configured with X2_NATIVE_TRACE=ON so the watch exists. Reconfigure without it for a normal run; the generated bodies are otherwise identical between the two.

### Note (2026-08-06)
HOP 6 — THE GATE IS ONE VIRTUAL CALL. Of FUN_005fac10's two stores, the reachable one is at 0x005faf4f and its guard is:

    005faf3f  MOV EAX,dword ptr [ESI + 0x20]
    005faf42  PUSH EAX                        ; [ESI+0x20]
    005faf43  PUSH 0x6a3a70                   ; a constant, almost certainly a string
    005faf48  CALL dword ptr [EDX + 0x5c]     ; vtable slot 23 on [ESP+0x14]
    005faf4b  TEST AL,AL
    005faf4d  JNZ 0x005faf7a                  ; true  -> carry on
    005faf4f  MOV byte ptr [0x006f3a2d],0x1   ; FALSE -> latch the failure

(with [ESI+0x24] pushed just before, so three arguments: a constant and two numbers that look like a size.)

The other store, 0x005fb194, sits immediately after a `RET 0x4` and is on a separate path.

**So the whole 'Display failed!' chain reduces to: a virtual call at slot 23 returned FALSE.** Six hops, each measured:

    slot-23 virtual returns false
      -> FUN_005fac10 sets 0x006f3a2d
      -> FUN_005fb270 reads it, sets 0x00a09f94
      -> FUN_00403420 sees that and raises the dialog

**NEXT**: identify the receiver — the object at [ESP+0x14] — and what its slot 23 is. Note the receiver is NOT igVkVisualContext: slot 23 of igVisualContext's vtable is one of the inherited platform-neutral ones (0x100513c2, shared by 9 classes), and our unimplemented reporter never fired. So this is a different object, most likely the display/window side rather than the renderer. Read 0x6a3a70 as a string first; it will probably name it outright.

This is where the renderer work resumes, and it is a read rather than a search.

### Note (2026-08-06)
HOP 7 — THE GATE IS igWin32Window::open, AND ITS ARGUMENT NAMES IT.

The constant pushed at 0x005faf43 reads, out of the PE:

    0x006a3a70 -> "X-Men Legends 2"

That is the WINDOW TITLE. So the slot-23 virtual is the display object's open -- `Gap::Display::igWin32Window::open`, which the boundary trace has shown being called all along -- taking (title, width, height) from [ESI+0x20]/[ESI+0x24]. The whole chain is now:

    igWin32Window::open("X-Men Legends 2", w, h) returns FALSE
      -> FUN_005fac10 latches 0x006f3a2d
      -> FUN_005fb270 reads it, sets 0x00a09f94
      -> FUN_00403420 raises "Display failed!"

**This is a WINDOW failure, not a renderer one.** It is consistent with the renderer evidence throughout: no slot we owe was ever dispatched, and the Vulkan device is created successfully every run.

**WHAT I COULD NOT SETTLE, and why** -- this matters before anyone spends time on it. Every run in this session was headless: the log says "GDI32: GetDeviceCaps answering from BUILT-IN DEFAULTS -- SDL could not report a display mode". Running WITHOUT --no-window still failed, but that proves nothing here, because there is no display for SDL to create a window on either way.

**So the first thing to do is re-run it on a real screen**: `./run.sh` (which is the native build, on your display, with sound). If "Display failed!" disappears, the whole chain above was the headless environment and the renderer's next demand is whatever comes after. If it persists WITH a display, then igWin32Window::open is genuinely failing and its own body is the thing to read.

Do that before implementing any renderer slot. Everything above is measured; this one question is not, and it decides whether there is a bug here at all.

### Note (2026-08-06)
HOP 8 — SETTLED UNDER A REAL DISPLAY, and it is upstream of the renderer entirely.

Ran under a private Xvfb (:77, 1024x768x24) rather than headless, so the 'there is no display' caveat in the note above is now DISCHARGED: SDL windows demonstrably work in this environment -- the vk_frame_path selftest creates one and presents 3 frames through it.

With a display, 'Display failed!' still appears, and the log says why:

    SDL: a real window exists; this is where the USER32/DINPUT/D3D8 surface lands.
    igVk: 0 frame(s) presented, 0 skipped for no swapchain texture, 0 with no window, ...

There is **no 'igVk: swapchain claimed on window ...' line**. That call only prints when `igvk_device_attach_window(win32_sdl_window())` succeeds, and `win32_sdl_window()` returns NULL unless `g_win_live` is set -- which only `CreateWindowExA` sets. So **the guest never called CreateWindowExA**. The window x2native reports at startup is its own, not the game's.

That is fully consistent with igWin32Window::open returning false, and it puts the failure UPSTREAM of the renderer: the game never got as far as asking for a window.

**So the question is now: what does igWin32Window::open do before CreateWindowExA, and which part of it fails?** It is in libIGDisplay, it is recompiled, and the boundary trace already shows it being entered -- so put X2_ARGS on it (needs the X2_NATIVE_TRACE build, which scratch/build-native currently is) and read its body. This is one function, entered, with a false return: the narrowest possible target.

Still true, and worth repeating because it has held through eight hops: NO renderer slot we owe has ever been dispatched. Implementing slots cannot move this.

### Resolution (2026-08-06)
FIXED, and the cause was USER32::GetDC returning NULL, not the renderer. igWin32Window::open calls CreateWindowExA then GetDC and treats a NULL DC as fatal; GetDC now returns a token for the main window, which is safe because the entire GDI surface this game imports is one function (GetDeviceCaps) and it ignores the HDC. With that the engine drives display init to completion -- swapchain claimed on the GUEST's window -- and demanded slots 38, 30 and 25 in turn, now implemented. The run reaches LoadLibraryA("cg.dll"), the NVIDIA Cg shader path (C112). C125.
