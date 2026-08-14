---
id: C185
kind: claim
status: holds
created: 2026-08-14
tags: controller,glyphs,input,re
---

## Claim

Xbox prompt bytes are delivered through the game's original physical-input name boundary only for Xbox-family SDL devices with the verified font pack active

## Evidence

FUN_00619e30 -> FUN_006281f0 was decompiled and its devkind/code tables read from XMen2.exe. tests/test_pad_glyphs.c calls the exact __wrap_fn_XMen2_006281f0 shipping body: A, Z+/LT, Z-/RT and POV return generated guest font bytes with RET 8 balance; a non-Xbox slot, LS without authored art, and a child process with the pack disabled all reach __real_fn. test_dinput_pad proves SDL Xbox360/One positive and PS5/standard negative. A real zero-argument launcher run reports a content-hash cache hit and replacement of both X2F_med_PC font files.

## What would falsify it

A real Xbox-class controller reaches a mapped code but the rendered prompt is not its SVG, a non-Xbox controller receives an Xbox byte, or make_pad_font and the generated C header disagree on any codepoint
