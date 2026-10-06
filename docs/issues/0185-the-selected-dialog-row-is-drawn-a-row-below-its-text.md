---
id: 185
title: the selected dialog row is drawn a row below its text at high output heights
status: resolved
symptom: at 3840x2160 the difficulty dialog's orange highlight covers the empty row under HARD instead of HARD; at 1080p it sits visibly low
state_items: S004
tags: rendering,ui,dialog,highlight,resolution,user-report
created: 2026-09-23
updated: 2026-09-23
---

# 0185 — the selected dialog row is drawn a row below its text

State item: S004

## Symptom

Reported by the user (3840x2160 borderless): the New Game difficulty
highlight is drawn one row below the selected item. Reproduced at 4K; at
1920x1080 the bar is low by a fraction of a row, and 800x600 is correct.

## Cause

Issue #133 corrected the selected row's Y/Z scale, whose retail formula
`1.336 - height * 0.0007` crosses zero at 2160 lines. The same caller in
`XMen2.exe FUN_005ea9e0` also builds the translation it passes to
`FUN_005707d0` from that scale:

```
0x005ead2f  fmuls [0x00686030]        ; 7.0 * scale
...
translation = (row + 1, -720.0, top - a + 7.0 * scale)
```

The override rewrote the two scale arguments and left the translation
computed from the collapsed retail scale, so the row had the right height at
the wrong place, off by `7 * (corrected - retail)`: zero through 600 lines,
about a row at 2160.

## Resolution

`x2_dialog_selection_offset_correction` (pure policy, unit-tested) returns
`7.0 * (scale - retail_scale)`; the override adds it to the translation's
third component, and only after checking the vector's recovered `-720.0`
middle component alongside the existing scale-formula match.

`tools/live_case.py selector-dialog-*` gained a placement check against the
800x600 share (where no correction applies): 800x600, 720 and 4K pass 16/16
with tops 0.2996, 0.3013, 0.3013. With the translation correction removed the
4K top is 0.3212 and the check fails. Pixel measurement of 800x600, 720,
1080 and 4K captures puts the bar within 0.2% of output height of the
reference at every size.
