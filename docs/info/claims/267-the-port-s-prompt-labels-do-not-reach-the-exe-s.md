---
id: C267
kind: claim
status: holds
created: 2026-08-26
tags: text,glyphs,prompts,renderer,overrides
depends: src/native/prompt_glyph_draw.c#x2_override_005ee780, src/native/prompt_labels.c#x2_override_00619e30
---

## Claim

The port's prompt labels do NOT reach the exe's glyph loop FUN_005ee780. prompt_labels.c rewrites the retail label in place as a NARROW byte string (WR8, strlen) carrying codepoints 0x80..0x93; FUN_005ee780 consumes WIDE strings. In a boot-direct tutorial run the label override fired 2264 times and not one prompt codepoint arrived at the glyph loop. The A/B settles that it is not a masking artefact: with X2_PROMPT_GLYPHS=0 the same run draws the same string population (4557 strings / 2281 non-ASCII, against 4541 / 2273 with the pack on) and the same above-256 control words 9d28 01f2 08e2. Enabling the pack changes NOTHING that reaches FUN_005ee780. So either the elements holding those labels never draw in this scenario, or they reach the screen by a path other than this glyph loop. This does not disturb C266: the 4541 strings that DO draw still go through the pair.

## Evidence

scratch/logs/promptdraw2-err.log (pack on) and scratch/logs/promptdraw-off-err.log (pack off), both X2_BOOT_MAP=act0/tutorial/tutorial1 X2_MAX_FRAMES=1200 --no-window --d3d8 --run; PROMPT DRAW report line in each, and the 'Xbox prompt names'/'Prompt labels' counts in the matching -run.log. The detector is proven able to answer YES by ctest prompt_glyph_draw (tests/test_prompt_glyph_draw.c), which drives x2_override_005ee780 over guest memory with a composed keycap label and sees it counted.

## What would falsify it

a run in which the prompt-label elements are demonstrably on screen (the Controls binding menu, or a tutorial HUD prompt confirmed visible in a screenshot) and the PROMPT DRAW report still counts zero prompt codepoints -- that would move the answer from 'not drawn in this scenario' to 'drawn by another path'; conversely, any run whose PROMPT DRAW line counts a prompt codepoint refutes the non-arrival outright
