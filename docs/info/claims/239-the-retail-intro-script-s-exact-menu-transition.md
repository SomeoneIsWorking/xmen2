---
id: C239
kind: claim
status: falsified
created: 2026-08-22
tags: boot,menu,rmlui
depends: src/native/boot_menu_transition.c#x2_boot_menu_open
falsified_on: 2026-08-22
---

## Claim

The retail intro script's exact menu transition is the no-argument BehavEd mainMenuExit handler at XMen2.exe 0x0049fb20

## Evidence

Ghidra FindStringRefs on mainMenuExit decompiled FUN_0049fb20 as: get console singleton, invoke vtable +0x18 with the executable string mainmenuexit, XOR EAX,EAX, plain RET. scratch/recomp/XMen2.json independently lists the same 20-byte body and RET with no stack-pop immediate. `x2_boot_menu_open` calls this exact handler with zero callback argument bytes; `startup.c` uses it only for the exact intro_normal console command, retaining retail main-menu initialization.

## What would falsify it

Falsified if the supported XMen2.exe maps 0x0049fb20 to a different instruction body, if the handler requires arguments/context not present in the listing, or a live boot-to-menu run does not reach the ordinary retail main menu.

## FALSIFIED 2026-08-22

Live Boot to menu at frame 669 displayed the retail current-game-will-end confirmation and recorded zero main-menu Build/Show calls. Decompiling the registered mainmenuexit handler at 0x005f27a0 shows why: 0x0049fb20 supplies no argument and therefore takes the dialog branch, not the direct menu branch.

> Anything that cited this claim as proof must be re-checked. Grep the repo for it.
