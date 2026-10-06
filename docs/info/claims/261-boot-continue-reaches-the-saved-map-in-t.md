---
id: 261
kind: claim
status: holds
created: 2026-08-25
tags: continue,boot,save,conversation,splash
depends: src/native/continue_runtime.c#x2_continue_boot_dispatch, src/native/continue_runtime.c#x2_override_004b1280, src/native/startup.c#x2_override_00402ba0, src/native/boot_player_selection.c#x2_boot_player_select_primary
---

## Claim

Boot=Continue reaches the saved map in the same player state a manual
menu-Continue produces, and it reaches it WITHOUT the splash wait, the intro
movies, the menu map or the menu. Two interceptions do it, and neither
touches the retail chain itself:

- `FUN_00402ba0` is the boot frontend's per-frame intro phase, called from
  the main frame loop `FUN_00401d70` at 0x00401e70 until the phase sets its
  own `[this+0x28] & 2` done bit. It holds the legal splash by comparing the
  clock against its own start stamp at `[this+0x24]` and a duration constant
  at 0x00680034. While the persisted boot mode asks for Continue, the start
  stamp is marked long past BEFORE the retail body runs, so the phase's own
  comparison -- unchanged, its constant unread -- passes on the first tick.
- the intro command the phase then issues is intercepted, and
  `x2_continue_boot_dispatch` runs the authoritative retail mode-3 chain
  right there (catalog leaf pickup, save-manager mode 3, header/device/file
  selection, state 0x1c, exact-leaf redirect). The boot's intro phase has
  already run `resetgame` and the save-manager init by then, so the chain
  sees the pristine state it expects. The LOAD SUCCESSFUL ack re-selects the
  primary player, which the payload's party writes key off.

The menu path is kept as the REFUSAL fallback only: anything the retail
manager declines falls back to `x2_boot_menu_open` and says so.

## Evidence

tools/live_case.py boot-continue, 13/13 on the rebuilt
`scratch/build-native` tree (2026-08-25): `BOOT SPLASH: intro phase start
stamp 0.487 -> -1000000000.0`, `BOOT MODE: ... dispatching the retail save
chain for autosave.save directly`, NO `menu/main_back` open in 696 [FILE]
lines, 0 movie opens, `act0/tutorial/tutorial1.pkgb`, four character
packages, 0020b STARTED 0x18->0x13 with line 0x41->0x40, conv_0020b_end
launched, controls unlocked, `current player index 0` with handle 0x301 ->
actor 0x08333010 -- the same actor the manual control resolves
(manual-continue 4/4). The intro command now fires at FRAME 7; before the
splash interception the same case reached it at frame 1366.

The splash report prints on the first tick either way, so a run in which the
override never fired is distinguishable from one in which it declined: the
cutscene-skip case (boot mode normal) prints `BOOT SPLASH: retail splash wait
left intact (boot mode normal, phase present)`. That negative is what caught
the first attempt at this claim -- it was measured against a stale
`scratch/build-native/x2native` and the override had never run at all.

## What would falsify it

A boot-Continue run that opens the menu for interaction (any input required
to reach the saved map), loads `menu/main_back`, plays an intro movie, ends
with `current player index -1` or all hero handles UNRESOLVED, or shows the
adjacent conversation entering 0x10 with no line selected. Also: a
`BOOT SPLASH:` line reporting the wait was left intact on a Continue boot, or
no such line at all.
