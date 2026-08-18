---
id: C208
kind: claim
status: holds
created: 2026-08-16
tags: boot,startup,override,map-load,console
---

## Claim

The exe's boot sequence runs `runscript menus/intro_normal` through a hardcoded "main" boot-map check, and the game's own `loadmap <map> 0 0` console command loads a level directly; an override replacing that one boot command loads a level at frame 33 instead of ~4200, with no input driving and 0 draws refused

## Evidence

FUN_00402ba0 (the "launchMap" handler the engine fires on the INIT event; decompiled, string xref at 0x00402cd1 -> 0x006801d4) hardcodes the boot map: after a 5-second gate and three engine-init phases (guarded by bits 8/4/2 of [this+0x28]) it does `registerCommand("INIT","launchMap",buf,0)` via console +0x14, `execute("resetgame")` via console +0x18, then `strncpy(buf,"main",0x40); _stricmp(buf,"main")` and -- always equal -- `execute("runscript menus/intro_normal")` (0x006801d4). The `else` branch, `FUN_005d8920()->[+0x6c](name,0)` = loadMap, is DEAD CODE in the shipped binary. intro_normal.py is six startMovie/waitsignal pairs then mainMenuExit; new_game.py is startMovie(cine01) -> waitsignal -> `loadMapKeepTeam("act0/tutorial/tutorial1")`.

`loadMapKeepTeam`'s handler FUN_004a0cc0 (registered at data table 0x68b688 next to the name string 0x68c020) does not load anything itself: it sprintf's `loadmap %s 0 0` and runs it through the console's command-line path +0x1c = FUN_0055c410 (0x0055c410; FUN_004a0d30 = loadMapChooseTeam is `0 1`). So the real loader is the `loadmap` console command, reached through the same console the boot script runs through.

The override __wrap_fn_XMen2_0055beb0 (console vtable +0x18 = FUN_0055beb0, the command executor; RET 0x4 __thiscall with the command string at [esp+4]) intercepts exactly the boot's `runscript menus/intro_normal` when X2_BOOT_MAP=<map> is set and feeds `loadmap <map> 0 0` through console +0x1c (FUN_0055c410) instead -- the same path loadMapKeepTeam uses. Unset or 0: pure pass-through. Boot-direct run (X2_BOOT_MAP=act0/tutorial/tutorial1, no input script, --no-window --d3d8 --run, X2_MAX_FRAMES=1500): "X2_BOOT_MAP: the boot's intro script is replaced by a direct map load" then "[FILE] X2_SHOT_AFTER_FILE=act0/tutorial matched packages/generated/maps/act0/tutorial/tutorial1.pkgb -- the scene gate is now OPEN" at frame 33; 1264 presents, 91,565 draws, refused 0, exit 0, screenshot 3000+ colours. The normal smoke path needs ~4200 frames and a six-press input script to reach the same level.

## What would falsify it

a run with X2_BOOT_MAP set that either never prints the override announcement (wrap not firing: dispatch-table reference not redirected), or prints it but never opens a matched level package (loadmap requiring team/difficulty state the boot never sets up); or a run without X2_BOOT_MAP whose boot differs from stock