---
id: C238
kind: claim
status: holds
created: 2026-08-22
tags: ui,menu,rmlui
depends: src/native/options_menu.c#open_settings
---

## Claim

Retail Options command callbacks open the shared modal RmlUi settings overlay

## Evidence

Parsed shipped UI/menus/main.engb: the Options item executes options_main. XMen2.exe 0x005f4900 registers options_main to 0x005f1fa0 and options to 0x005f1c50; both original callbacks push the retail options menu. Ctest options_menu calls the production overrides and proves both registrations, plain RET stack effect, shared visibility capture, and retained F1 toggling.

## What would falsify it

A shipped menu stops using options_main/options, the executable command registry maps either name elsewhere, or ctest options_menu fails.
