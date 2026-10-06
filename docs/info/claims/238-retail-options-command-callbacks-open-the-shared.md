---
id: C238
kind: claim
status: falsified
created: 2026-08-22
tags: ui,menu,rmlui
depends: src/native/options_menu.c#open_settings
falsified_on: 2026-08-24
---

## Claim

Retail Options command callbacks open the shared modal RmlUi settings overlay

## Evidence

Parsed shipped UI/menus/main.engb: the Options item executes options_main. XMen2.exe 0x005f4900 registers options_main to 0x005f1fa0 and options to 0x005f1c50; both original callbacks push the retail options menu. Ctest options_menu calls the production overrides and proves both registrations, plain RET stack effect, shared visibility capture, and retained F1 toggling.

## What would falsify it

A shipped menu stops using options_main/options, the executable command registry maps either name elsewhere, or ctest options_menu fails.

## FALSIFIED 2026-08-24

The product policy changed on 2026-08-24: retail options/options_main remain retail-owned. Port Settings is a distinct derived pause row and additive port_settings command; the F1 path and both retail callback overrides were removed.

> Anything that cited this claim as proof must be re-checked. Grep the repo for it.
