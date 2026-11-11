---
id: 207
title: a virtual pad attached at the main menu is sometimes never read
status: open
symptom: "pad 0 connected, HOTSWAP and the session assignment all log, /pad presses are set, and the game reads the pad 0 times"
state_items:
tags: input,tooling
created: 2026-11-12
updated: 2026-11-12
---

# 0207 — a virtual pad attached at the main menu is sometimes never read

## Observed

`scratch/lobby/two.py` attaches `X2_VIRTUAL_PAD`, waits for `DINPUT-PAD:
pad 0 connected` and `DINPUT8: HOTSWAP`, assigns player 1 to pad 0 and
presses the d-pad on the main menu. Three runs on 2026-11-11 walked the
rows. One on 2026-11-12, same script and namespaces, never moved the focus:
the control channel set every press and the run's report says `the game
read a button 0 time(s)`.

## Next

Find which reader of the pad stays unbound in the failing runs: whether the
game's enumeration at `0x00628e20` ran before the assignment took, and
whether a player's real pad plugged in at the main menu meets the same.
