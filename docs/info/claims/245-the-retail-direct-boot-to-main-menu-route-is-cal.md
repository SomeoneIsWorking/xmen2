---
id: C245
kind: claim
status: holds
created: 2026-08-22
tags: boot,menu,re
depends: src/native/boot_menu_transition.c#x2_boot_menu_open
---

## Claim

The retail direct boot-to-main-menu route is callback 0x0049fb00: it issues mainmenuexit 1, whose handler at 0x005f27a0 takes the non-empty branch, runs resetgame, and submits loadmap menu/main_back without constructing the exit confirmation.

## Evidence

Ghidra decompilation of XMen2.exe FUN_0049fb00, FUN_0049fb20, and registered command handler FUN_005f27a0; test_boot_menu_transition pins the production bridge to RVA 0x0009fb00 with plain-RET ABI.

## What would falsify it

A corrected live Boot to menu run opens a confirmation or otherwise fails to reach one retail CMenuMain Build/Show after the forced callback is dispatched.
