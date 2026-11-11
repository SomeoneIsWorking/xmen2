---
id: 207
title: a virtual pad attached at the main menu is sometimes never read
status: resolved
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

## Cause

`src/native/dinput8.cpp:m_EnumDevices` took the pvRef of every
`EnumDevices(GAMECTRL)` as the game's input manager, and the hotswap pump
re-enumerated that object. The game's gamepad-scan thread `FUN_00618c90`
enumerates into its own scratch manager (`0x00a6a7e4`) on a timer the main
menu runs, so once the menu was up the pump admitted a pad into the scratch
manager. The frame update polls the manager at `0x00a6abfc`, which never got
the pad. A player plugging a pad in at the main menu meets the same. A pad
attached during the intro, before the first scan, was admitted correctly.

## Fix

The pump and the slot translation read the input manager from `0x00a6abfc`
each time; nothing keeps a pvRef.

Proven 2026-11-12 with `scratch/lobby/padprobe.py` and `X2_VIRTUAL_PAD=f4000`,
a pad attached with the main menu up: before the fix, two of two runs read the
pad 0 times and the focus stayed on `label_option04`. After it, two of two
runs read it about 350 times, and three d-pad presses moved the focus to
`label_option07`. The early attach (`f600`) still moves.
