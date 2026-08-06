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
