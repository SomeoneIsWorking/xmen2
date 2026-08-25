---
id: 261
kind: claim
status: holds
created: 2026-08-25
tags: continue,boot,save,conversation
depends: src/native/continue_runtime.c#x2_override_005c9260, src/native/continue_runtime.c#x2_override_004b1280, src/native/boot_player_selection.c#x2_boot_player_select_primary, tools/check_continue_wiring.py#audit_boot_dispatch
---

## Claim

Boot=Continue reaches the saved map in the same player state a manual
menu-Continue produces: the retail menu-map lifecycle completes by design,
CMenuMain::Show is intercepted to select the primary player through
CPadManager's own setter and dispatch the retail Continue chain synchronously
(Hide, then the registered callback, mode-3 save manager, exact-leaf
redirect), and the LOAD SUCCESSFUL ack re-selects the primary player because
the intervening menu lifecycle clears CPadManager and the payload keys its
party writes off that player. The replayed level conversations then play
healthy -- the adjacent conversation starts VISIBLE with a selected line, no
seen-bit collision -- and the player can advance or skip them to the normal
control unlock.

## Evidence

tools/live_case.py boot-continue, 12/12 on the rebuilt tree (2026-08-25):
BOOT MODE + BOOT PLAYER log lines, menu/main_back pkgb (lifecycle, by
design), act0/tutorial/tutorial1.pkgb, four character packages, 0020b
STARTED 0x18->0x13 with line 0x41->0x40, conv_0020b_end launched, controls
unlocked after an Escape, `current player index 0` with handle 0x501 ->
actor 0x082e7010 -- the same actor the manual-continue control run resolves.
The manual control itself: manual-continue 4/4. Before the ack re-selection
the same run ended index -1 with every handle UNRESOLVED and 0020b went
0x18->0x10 with no line (issue #83's signature) -- the write-watch sequence
select 0 / Show clear / Hide restore / two later clears is in issue #113.

## What would falsify it

A boot-Continue run that opens the menu for interaction (any input required
to reach the saved map), ends with `current player index -1` or all hero
handles UNRESOLVED, or shows the adjacent conversation entering 0x10 with no
line selected.
