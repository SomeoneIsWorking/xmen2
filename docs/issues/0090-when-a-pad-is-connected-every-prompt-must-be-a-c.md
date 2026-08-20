---
id: 90
title: When a pad is connected, EVERY prompt must be a controller prompt
status: resolved
symptom: with a controller connected, some prompts show controller glyphs and others still show keyboard keys -- the game is inconsistent about which device it is telling you to use
tags: pc,native,input,pad,glyphs,prompts,hotswap,user-report
created: 2026-08-19
updated: 2026-08-21
---

REQUIREMENT stated by the user, 2026-08-19: "when hotswapped to a controller,
all prompts need to be controller". This is the acceptance bar for the feature,
not a bug report about one screen -- a prompt that names the wrong device is
worse than no prompt, and a screen that mixes the two is worse still.

Related: #87 (gameplay popup prose), #85 and #86 (rows with no pad binding).

## What had to be covered

Four independent paths all have to hold:

1. **The row has to HAVE a pad binding.** A row bound only to a key cannot be
   named with a button, whatever the label code does. 22 of the game's 42 rows
   carry the canonical pad binding (measured, `x2ctl.py input`), including
   Power and TargetLock/Use Health Pack. The remaining quick-power rows are PC
   keyboard conveniences rather than console prompt actions.
2. **The label has to be SELECTED from the pad binding.** `FUN_006294b0` is
   overridden to do that, and it is measured working (C215, 7,873 of 7,873
   reads in one run) -- but only for labels that come through `FUN_00619e30`.
   The completed direct-caller census found no missing gameplay label path:
   `FUN_006281f0` has only `FUN_00619e30` (action labels) and `FUN_00625840`
   (controller-list rendering) as callers. `FUN_00619e30` is called only by
   `FUN_004bd720`, the generic localized `$ACTION` token expander. The earlier
   6,491/zero run observed the conversation-specific `$MENU_ACCEPT` path and
   did not disprove action-token coverage.
3. **The glyph has to exist for the code.** `pad_glyph_code` now answers for
   the face buttons, LB/RB, Back/Start, both triggers, all four d-pad
   directions, and both stick clicks (0x1d/0x1e). The LS/RS gap is fixed by
   publishing the shared `port-assets` glyphs and mapping both physical codes;
   the shipping-wrapper test covers the complete inventory.
4. **Literal tutorial prose has to follow the device too.** The gameplay popup
   was not an uncovered `$ACTION` caller. `CPopupDialog::create`
   (`FUN_005ebbc0`) replaced eight localized dialog assets with PC-only
   `igct.bnx` strings containing mouse and shortcut-key prose. The localized
   assets already carry the controller-authored text. `dialog_prompts.c` now
   retains that asset text only for this exact localization call when a
   controller is present, while retaining the PC string on keyboard.

## Verification

The direct caller census remains complete: localized `$ACTION` tokens enter
through `FUN_004bd720 -> FUN_00619e30`, and `FUN_006281f0` has no third gameplay
naming caller. The popup investigation additionally enumerated all eight
hardcoded PC tutorial replacements in `FUN_005ebbc0`; they share the one scoped
localization boundary now covered by `dialog_prompts.c`.

A natural, windowless `switching_hint` run ended with 7,259 of 7,259 prompt
labels selecting pad bindings, 7,259 glyph names, zero original names and zero
unchanged labels. The popup-text counter recorded one controller asset, zero PC
overrides and eight unrelated localization lookups. The capture contains only
controller instructions, including d-pad/A glyphs, with no `[LEFT CLICK]` or
`[???]`. This is both a natural gameplay observation and a complete measured
denominator for every prompt label encountered in that run.
