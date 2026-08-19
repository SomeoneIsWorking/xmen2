---
id: 87
title: In gameplay the prompts still name keyboard keys, not the pad
status: open
symptom: with a pad connected and the derived font pack active, in-game prompts still show keyboard key names instead of the Xbox button glyphs
tags: pc,native,input,pad,glyphs,prompts,hud,fonts,user-report
created: 2026-08-19
updated: 2026-08-19
---

REPORTED BY THE USER, 2026-08-19, from a real play session with a pad: "it only
shows keyboard keys".

## Why this contradicts what is recorded, and what that narrows

C219 captured the glyphs rendering IN GAME -- the main menu prompt bar drawing
`B BACK` / `A SELECT` with the real button art, and the tutorial dialog drawing
the button in place of `[ENTER] CONTINUE...` -- and C215 measured the label
following the pad, 7,873 of 7,873 label reads in one run. Those captures stand;
they were on the MENU and the CONVERSATION path.

This report is about GAMEPLAY, which neither of them covered. That is a
different screen, drawn by different widgets, and there are two known reasons a
prompt there could still read as a keyboard key -- both already written down as
gaps, neither yet tested:

1. **The font.** `docs/roadmap.md` feature 3 and `docs/features/xbox-button-prompts.md`
   both record it explicitly: three of the four fonts the game loads
   (`X2F_big`, `X2F_hud_PC`, `font_XMEN_digital`) are pixel format 15, the
   builder refuses to re-encode them, and a prompt drawn in one of those would
   be BLANK. The HUD font is the obvious suspect for a gameplay prompt. But
   note the user reports keyboard NAMES, not blanks -- a missing glyph would
   draw nothing, so if the text reads `[ENTER]` in words then the LABEL is
   keyboard, and the font is not the cause. Establish which of the two is on
   screen before choosing a direction; that single observation splits the
   investigation.
2. **The label path.** The 2026-08-18 measurement in the feature doc found that
   the dialog prompt does not come through `FUN_00619e30` at all: `FUN_006281f0`
   ran 6,491 times with no gamepad device kind, and `FUN_006294b0` ran ZERO
   times. That caller census was never finished. A gameplay HUD prompt is very
   likely another caller that the two label overrides do not cover.

## What the next session should do first

1. Get the screen: a boot-map run to a level with `X2_SHOT_AFTER_FILE` gating
   the capture, then LOOK at whether the prompt is blank or spells a key name.
2. If it spells a key name: finish the caller census the feature doc asks for.
   `src/native/pad_glyphs.c` already counts every call to the naming boundary
   and can record its callers -- that is the tool, and it exists.
3. If it is blank: the format-15 font gap is real and in the way. Establish
   what pixel format 15 actually is before concluding it cannot be encoded --
   "the builder refuses it" is a rule this port wrote, not a property of the
   format, and the refusal was written to prevent a wrong re-encode, not
   because one is impossible.

Related: #85 and #86 are the input side of the same play session. If the pad
has no row for an action, its prompt has nothing to name either, so check
whether the prompts that read as keyboard are for actions the preset never
bound -- that would make all three one cause.
