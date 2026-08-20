---
id: 90
title: When a pad is connected, EVERY prompt must be a controller prompt
status: open
symptom: with a controller connected, some prompts show controller glyphs and others still show keyboard keys -- the game is inconsistent about which device it is telling you to use
tags: pc,native,input,pad,glyphs,prompts,hotswap,user-report
created: 2026-08-19
updated: 2026-08-20
---

REQUIREMENT stated by the user, 2026-08-19: "when hotswapped to a controller,
all prompts need to be controller". This is the acceptance bar for the feature,
not a bug report about one screen -- a prompt that names the wrong device is
worse than no prompt, and a screen that mixes the two is worse still.

Related and probably the same work: #87 (gameplay prompts still name keyboard
keys), #85 and #86 (rows with no pad binding).

## Why this is not just "fix one more caller"

Three things all have to hold, and today none of them is established
everywhere:

1. **The row has to HAVE a pad binding.** A row bound only to a key cannot be
   named with a button, whatever the label code does. 22 of the game's 42 rows
   now carry the canonical pad binding (measured, `x2ctl.py input`), including
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
   the shipping-wrapper test covers them. This closes the glyph inventory,
   but it does not prove the row and caller coverage in points 1 and 2.

## How to know it is done

Not by looking at one screen. The counters in `src/native/pad_glyphs.c` already
report `N glyph(s), M original name(s)`; the gate is that with a pad connected
**M is zero for pad-bound rows**, with its denominator printed, plus a caller
census showing every label path is covered. A screenshot of one prompt is what
made this look finished twice already.
