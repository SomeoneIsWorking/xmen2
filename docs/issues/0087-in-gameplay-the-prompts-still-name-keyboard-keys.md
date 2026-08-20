---
id: 87
title: In gameplay the prompts still name keyboard keys, not the pad
status: open
symptom: with a pad connected and the derived font pack active, in-game prompts still show keyboard key names instead of the Xbox button glyphs
tags: pc,native,input,pad,glyphs,prompts,hud,fonts,user-report
created: 2026-08-19
updated: 2026-08-20
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
2. **The label path.** The earlier conclusion that gameplay likely used an
   uncovered caller was wrong. The completed direct-caller census found exactly
   two callers of `FUN_006281f0`: the action-label builder `FUN_00619e30` and
   the retained controller-list renderer `FUN_00625840`. The action builder has
   one caller, `FUN_004bd720`, the generic localized-token expander. It maps the
   `0xf000 | action` token class to `FUN_00619e30`; the tutorial's `$POWER`,
   `$GUARD`, `$MOVE`, `$ATTACK`, `$SMASH`, `$ALLY`, and `$TARGET_LOCK` tokens
   therefore all reach the current pad-selection and glyph overrides. The 6,491
   calls measured on the conversation screen came from unrelated label-table
   construction, while `$MENU_ACCEPT` has its own conversation renderer.

## What the next session should do first

1. Trigger one of the shipped gameplay hint scripts naturally and capture it
   in a windowless run with the derived pack active. A scratch script that
   called `createPopupDialogXml` directly never created the widget, so that
   failed injection is not a negative prompt result.
2. If it spells a key name, record the token/action and the label counters at
   that frame; the direct-call inventory is complete, so the next distinction
   is wrong row selection versus a non-action literal string.
3. If it is blank, the format-15 font gap is real and in the way. Establish
   what pixel format 15 actually is before concluding it cannot be encoded --
   "the builder refuses it" is a rule this port wrote, not a property of the
   format, and the refusal was written to prevent a wrong re-encode, not
   because one is impossible.

Related: #85 and #86 are resolved. The canonical preset now has 22 pad-bound
rows, including Power and TargetLock/Use Health Pack. QuickPower rows remain
keyboard-only PC conveniences and are not console tutorial actions.
