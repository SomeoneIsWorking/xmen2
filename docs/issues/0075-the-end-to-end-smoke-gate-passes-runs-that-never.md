---
id: 75
title: The end-to-end smoke gate passes runs that never left the main menu
status: resolved
symptom: The old scripted smoke scenario reports PASSED, with all six scripted presses fired, no refused draw and a bright final frame -- for a run that answered the difficulty dialog a few frames late, backed out to the main menu, and sat in the menus for its whole 4200 frames.
tags: pc,native,tooling,instruments,gate
created: 2026-08-14
updated: 2026-08-14
---

## Root cause

The gate's check 2 was an INFERENCE, written in its own header: 'the run
reached the last press, which is only reachable through a level'. That is false
for frame-scheduled input. X2_INPUT_SCRIPT fires each press at a frame number
whether or not the game is anywhere near the state the press was written for,
so 'all six fired' only proves the run kept presenting.

Measured: two runs with the shipped script fired every press, opened 377
distinct files and never opened the level package, while a run whose Returns
REPEATED opened 773 and loaded it. The difference is timing jitter on one
press: a Return that lands before the difficulty dialog is open leaves the run
in the menus for good, and the later Escapes then back out to the main menu.

## Fix

1. Check 2 now reads the game's own answer to 'where am I': the run sets
   X2_SHOT_AFTER_FILE (default act0/tutorial) and the gate requires the
   '[FILE] ... the scene gate is now OPEN' line. That also aims the
   screenshot at a level frame, so check 4 stops being about a menu.
2. The two menu answers repeat instead of being single presses:
   f2600-2900/50:Return and f3150-3260/40:Escape. Extra Returns are harmless
   once the level is up.

## Proof it can fail

The scenario selftest gains 'a run that never loaded a level': the
known-good log with the gate line REMOVED must fail. 14 of 14 cases pass,
including that one.
