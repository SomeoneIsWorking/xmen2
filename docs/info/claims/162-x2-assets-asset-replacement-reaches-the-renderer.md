---
id: C162
kind: claim
status: holds
created: 2026-08-12
tags: assets,fonts,glyphs
---

## Claim

X2_ASSETS asset replacement reaches the renderer: a substituted font changes the glyphs actually drawn

## Evidence

With x2f_big.igb substituted over x2f_hud_pc, x2f_med_pc and font_xmen_digital, the cutscene caption's second line rendered as 'G' instead of 'GREENLAND' -- wrong glyphs through unmatched metrics -- while the first line, whose font was the substitution SOURCE, was unchanged. The Xbox HUD font in the same position gives 0 refused draws and unchanged Latin text, which is what a correct swap looks like since the two fonts differ in button glyphs rather than letters.

## What would falsify it

A substitution whose replaced-file line appears in the report but whose pixels do not change when the substituted font is one the surface under test is definitely NOT already drawn in. Note the trap that produced a false negative here (issue #60): substituting the font a caption already uses is a no-op, not a control.
