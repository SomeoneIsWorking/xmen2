---
id: C035
kind: claim
status: holds
created: 2026-08-05
tags: 
---

## Claim

The Xbox build of X-Men Legends II lifts CLEANLY through the existing xboxrecomp toolchain: 21,907 functions, 21,907 translated, 0 failed, 1,528,337 lines of C, in a single pipeline run.

## Evidence

vendor/xboxrecomp (sp00nznet) run on scratch/xbox_iso/default.xbe. xbe_parser identifies it correctly (title 'X-Men Legends 2: Rise of Apocalypse', title ID 0x41560047, build 2005-08-26, entry 0x00225A09, 21 sections) and even recovers the original build path c:\\work_builds\\XMen2\\Code\\Engine\\Xbox_EXE. disasm found 21,907 functions in 29s -- comparable to Burnout 3's 22,097, the title that project lists as playable. func_id classified them in 7s (7,143 by vtable scan, 5,365 vtable thunks, 1,149 XDK callers). recomp lifted every one with zero failures and 7,995 unresolved stubs.

## What would falsify it

LIFTED, not built and not run: the generated C has not been compiled, no runtime shims are wired, and 7,995 unresolved stubs remain. The project's own README says the first run of any recompiled game crashes and that indirect calls are 'the single hardest challenge' -- which is exactly the wall the PC effort hit independently. A clean lift is the easy part.
