---
id: 87
title: In gameplay the prompts still name keyboard keys, not the pad
status: resolved
symptom: with a pad connected and the derived font pack active, in-game prompts still show keyboard key names instead of the Xbox button glyphs
tags: pc,native,input,pad,glyphs,prompts,hud,fonts,user-report
created: 2026-08-19
updated: 2026-08-21
---

REPORTED BY THE USER, 2026-08-19, from a real play session with a pad: "it only
shows keyboard keys".

## Root cause

The gameplay action tokens were already correct. The missing path was the
popup's surrounding prose. `CPopupDialog::create` (`FUN_005ebbc0`) loads the
localized dialog asset, then replaces the text of eight PC tutorial dialogs
with keys from `igct.bnx`. Those PC-only strings name mouse controls and
keyboard shortcuts, while each localized dialog asset already contains the
controller-authored version.

`src/native/dialog_prompts.c` now scopes the shared localization lookup
`FUN_00629bf0` to its one call from `0x005ec061`. When a controller is connected,
it asks the already-loaded dialog parser for its own `text` field, using the
same parser getter as the ordinary retail branch. With no controller it retains
the PC override. Other localization calls always retain the recompiled body.
The decision is made for each popup, so controller attach/detach cannot leave a
cached device-specific string behind.

## Verification

A windowless real run booted `act0/tutorial/tutorial1`, advanced the retail
conversations, walked to the switching terminal and triggered the shipped
`switching_hint` script naturally. The resulting popup contains the d-pad and
A-button glyphs with the controller-authored movement/selection text. It has
neither `[LEFT CLICK]` nor `[???]`.

The live shutdown report carried the denominators:

    Xbox prompt names: 7259 glyph(s), 0 original name(s)
    Xbox prompt rows: 7259 label reads -- 7259 pad, 0 no-pad
    Prompt labels: 0 keycap, 7259 pad, 0 unchanged
    Tutorial dialog text: 1 controller asset, 0 PC override,
                          8 unrelated localization lookups
    scripts: switching_hint launched 1 time

`tests/test_dialog_prompts.c` independently covers the scoped return address,
the controller/no-controller split, unrelated localization calls, mapped
addresses, parser ABI, return value and guest stack effect.

Related: #85 and #86 are resolved. The canonical preset now has 22 pad-bound
rows, including Power and TargetLock/Use Health Pack. QuickPower rows remain
keyboard-only PC conveniences and are not console tutorial actions.
