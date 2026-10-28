---
id: 200
title: touch-worldmap and touch-team-gear stop before reaching their menus
status: resolved
symptom: "touch-worldmap: loadmap act1/sanctuary/sanctuary1 never reaches gameplay; touch-team-gear: the XP grant raises no level-up popup"
state_items: S025
tags: touch,live-case
created: 2026-10-28
updated: 2026-10-28
---

# 0200 — touch-worldmap and touch-team-gear stop before their menus

## Observed

`tools/live_case.py touch-worldmap`: 4 of 11. `loadmap act2/savage/savage1`
reaches gameplay. The next `loadmap act1/sanctuary/sanctuary1` does not, so
the world map never opens. `tools/live_case.py touch-team-gear`: "the XP grant
raised the level-up popup" fails (`dismiss_level_up` in `tools/live_menu.py`),
then the case raises a `TypeError` on a missing touch menu.

Both fail the same way on `bcae6e5`, built as a baseline on 2026-10-28, so the
list box touch layout did not cause them. `docs/project-state.md` last records
them passing on 2026-10-07 (16 of 16 and 20 of 20).

## Cause

Both runs crash (`SIGSEGV` at address 0) in the world scene's attach
(`0x0059b0c0`, vt `+0x24`) called from `x2_override_005e90d0` in
`src/native/popup_touch.cpp`. `PopupTouch::load_bars` loaded the touch
popup's eleven bar entities once, at the first front-end attach, and kept
them for the whole run; the map load releases the popup's models
(`FUN_005e9070`, vtable slot 2) and frees that art, so the next popup in
gameplay (the level-up popup, or the conversation's popup after the second
`loadmap`) attached freed entities (a bar's vtable read `0x0069c204` at load
and `0x0069c074` at the crash). Introduced with the bar cards in `ece93ca`.

`touch-team-gear` then also bought its gear through the RmlUi touch menu's
shop, which is no longer drawn since `CMenuShop` became native on touch.

## Fix

`popup_touch.cpp` takes `FUN_005e9070` and releases the bars with the popup's
own models; the next attach loads them again. `touch-team-gear` buys through
the game's shop the way `touch-stash` does (the buy tab, the list's cards,
the `$MENU_OK` button).

On 2026-10-28: `touch-worldmap` 19 of 19, `touch-team-gear` 21 of 21, no
crash in either run.
