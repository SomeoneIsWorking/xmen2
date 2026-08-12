---
id: C167
kind: claim
status: holds
created: 2026-08-12
tags: input,prompts
---

## Claim

The on-screen control prompt is BUILT AT RUN TIME from the DirectInput object name, not stored: FUN_00619e30 formats sprintf("[%s]", FUN_006281f0(devkind, code)) using the first non-empty binding slot in the order 2,0,1,1. Slot 2 is the pad slot, so a populated pad binding already makes the label follow the controller with no new policy.

## Evidence

Disassembly of FUN_00619e30/FUN_006281f0/FUN_006294b0/FUN_00629210 from scratch/recomp/XMen2.json; the literal [%s] at 0x6a4e64 is the only such format in the exe and has exactly one xref, 0x619f74. A scan of all 13,382 files / 2368.5 MB of the PC install for [Enter] (ASCII and UTF-16), Enter] and ENTER] returns 0 hits, and no binary contains the bare word Enter/Escape/Backspace.

## What would falsify it

if a run shows a pad-bound action still labelled with a keyboard name while slot 2 holds a non-zero device kind, the slot order is not 2,0,1,1 as read
