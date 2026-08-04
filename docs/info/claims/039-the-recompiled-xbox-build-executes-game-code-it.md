---
id: C039
kind: claim
status: holds
created: 2026-08-05
tags: 
---

## Claim

The recompiled Xbox build EXECUTES GAME CODE: it maps the Xbox memory layout, loads all 21 XBE sections, resolves 156/156 kernel thunks, enters the game entry point and runs until the game asks the kernel to create its main thread.

## Evidence

Run output: 64MB base view mapped at 0x10000, 21 sections loaded, 'Kernel thunk bridge: 156/156 resolved (68 bridged, 88 stub)', entry 0x00225A09 with ESP 0x00F7FFF0, then two real kernel calls made BY GAME CODE -- PsCreateSystemThreadEx(routine=0x0022286B) and NtClose -- followed by a clean exit. The crash that blocked this was root-caused with gdb: the Xbox base view mmaps 64MB at 0x10000 with MAP_FIXED, spanning 0x10000-0x4010000, and the host executable was non-PIE at 0x400000 -- INSIDE that range -- so the mapping silently unmapped our own running code. Fixed by building everything position-independent so the loader places the host clear of the guest address space.

## What would falsify it

The game does not proceed: its main thread's start routine 0x0022286B was never identified as a function by the disassembler, so it is absent from the dispatch table and PsCreateSystemThreadEx cannot start it. Nothing renders and no window is created. This is the same runtime-discovered-indirect-target problem the PC effort hit, and upstream calls indirect calls 'the single hardest challenge'.
