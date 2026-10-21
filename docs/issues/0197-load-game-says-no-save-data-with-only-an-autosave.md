---
id: 197
title: Load Game says there is no save data when only an autosave exists
status: resolved
symptom: Continue loads a level, but Load Game shows "No X-men Legends 2 save data present on hard disk." instead of the autosave
state_items: S015
tags: save,autosave,load-menu,user-report
created: 2026-10-21
updated: 2026-10-21
---

# 0197 — Load Game says there is no save data when only an autosave exists

## Reproduction

A profile whose save directory holds `autosave.save` and no `saveslot*.save`:
main menu, Load Game. Observed: EMSG_NO_DATA_DEVNUM with Cancel/Retry.
Expected: the autosave row that issue #131 added.

## Root cause

`XMen2.exe 0x004b0800` (save-manager state 0x11) refuses load mode with
message 7, EMSG_NO_DATA_DEVNUM, while manager `+0xcc` is below one. That count
comes from `0x0055f190`, which counts only `saveslot*.save`, so the dialog
never reached state 0x19 (`0x004b0cf0` -> `0x004b0d20`), where #131's
projection adds the autosave row.

## Resolution

`load_game_menu_runtime.cpp` overrides `0x004b0800` and, in load mode, adds the
present autosaves to `+0xcc` before the retail body runs. The same change
replaced the single `autosave.save` with five rotating slots; see
[Boot](../RE/boot.md).

Live: with only `autosave0.save`, Load Game listed "AUTOSAVE - 00:00 - Jungle
Canal (Normal)" and loaded it.

## Found on the way

The autosave header parser required the tag's closing `]`, but `0x00468990`
writes the tag into a fixed 0x32-byte field, so on levels with long names
(Southern Habitats) every autosave there failed `header-failed`.
`autosave_header_from_payload` now ends the description at `]` or at the
field's NUL.
