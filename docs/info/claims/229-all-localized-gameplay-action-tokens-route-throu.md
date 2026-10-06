---
id: C229
kind: claim
status: holds
created: 2026-08-21
tags: input,pad,glyphs,prompts,re
depends: src/native/pad_glyphs.c#x2_override_00619e30, src/native/pad_glyphs.c#x2_override_006281f0
reconfirmed: 2026-08-21
verified_at: 2026-08-21 01:35:10
---

## Claim

All localized gameplay action tokens route through the existing pad-aware action-label builder; there is no third gameplay caller of the physical-input name function

## Evidence

Ghidra direct-call references in XMen2.exe give exactly two callers of FUN_006281f0: FUN_00619e30 (action labels) at 0x00619f69 and FUN_00625840 (controller-list rendering) at 0x00625b73. FUN_00619e30 has exactly one direct caller, FUN_004bd720 at 0x004bd739; its decompilation recognizes token class 0xf000, masks the low action byte, and calls the label builder. Decoded tutorial dialogs use `$POWER`, `$GUARD`, `$MOVE`, `$ATTACK`, `$SMASH`, `$ALLY`, and `$TARGET_LOCK`. The failed scratch popup injection produced no widget and is explicitly not live prompt evidence.

## What would falsify it

an indirect or direct gameplay prompt path is observed to name a bound action without passing FUN_004bd720/FUN_00619e30, or a localized gameplay action token bypasses the current label override

## Re-confirmed 2026-08-21

Reconfirmed after prompt_labels changes: a natural windowless switching_hint run measured 7,259 of 7,259 label reads selecting the pad binding and 7,259 glyph names with zero original names; the direct Ghidra caller census remains FUN_004bd720 -> FUN_00619e30 -> FUN_006281f0
