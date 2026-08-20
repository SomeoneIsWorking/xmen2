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
   named with a button, whatever the label code does. 21 of the game's 42 rows
   carry a pad binding today (measured, `x2ctl.py input`); the other 21 include
   the whole quick-power system. So "all prompts controller" is bounded below
   by #85/#86.
2. **The label has to be SELECTED from the pad binding.** `FUN_006294b0` is
   overridden to do that, and it is measured working (C215, 7,873 of 7,873
   reads in one run) -- but only for labels that come through `FUN_00619e30`.
   The 2026-08-18 census found the tutorial dialog does NOT: `FUN_006281f0` ran
   6,491 times with no gamepad device kind and `FUN_006294b0` ran ZERO times.
   That census was never finished, and finishing it is the main task here.
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
