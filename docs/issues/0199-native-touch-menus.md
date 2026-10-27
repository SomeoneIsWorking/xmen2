---
id: 199
title: Native touch menus: the game's own menus answer a finger, nothing drawn over them
status: open
symptom: "Touch menus are a port-styled RmlUi panel over the retail menu, and the other menus get a drawn controller pad"
state_items: S025, S020
tags: touch,menus,ui
created: 2026-10-27
updated: 2026-10-27
---

# 0199 — Native touch menus

## Goal

On touch, the retail menu is the UI: drawn by the game in its own art and style,
and touched directly. Nothing is drawn over a menu, neither the RmlUi touch menu
nor the menu pad. Anything touch needs that the game lacks is drawn by the game's
renderer from the player's install.

## Work list

`tools/scan.py` opens every menu its console can open (`boot.mode=menu`, then
`X2_BOOT_MAP=act2/jungle/jungle1` and `openmenu <name>`) with touch forced on, and
prints the run-end `menu touch gap:` lines from `x2::input::MenuTouchGapReport`
(`src/input/menu_touch_gaps.cpp`). Each kind names the guest function that
refuses the finger:

| kind | refused by | seen on |
|---|---|---|
| row without command | `CMenuItem::onMouse` `0x005bc1b0` (no `usecmd`) | main: danger room, Play Online; pda: Objectives, Blink Portal, Small Map, Players; options_controller: its label rows |
| step | `CMenu::onMouse` `0x005ae0a0` (no left/right) | options: Music Volume, Effects Volume |
| list | list box `onMouse` `0x005c0e10` (no scroll) | shop, codex, review, danger_room, trivia |
| footer token | `CMenuItemText::onMouse` `0x005c6490` | codex: `$MENU_DETAILS` |
| popup | `popup+0x7c`, not mapped | main on a fresh profile |
| small target | the menu's own layout | every menu: each clickable row, footer and list row is under a tenth of the shorter client edge at 1280x720 |

Not reached by any route yet, so not in the list: the team menu's roster and a
hero's detail tabs (LT Details, RT Next hero, RB Drop and Assign), the world
map's points (no unlocked point at the boot map), and `options`/`options_controller`
from the title menu (a popup is up there).

## Steps

1. A finger is the retail pointer on every menu: down moves it, up clicks.
2. Rows without a command, steps and lists answer a tap at their owners above.
3. Footer tokens the game does not publish get their action at `CMenuItemText::onMouse`.
4. The menu scene is scaled for touch so the smallest target is finger-sized.
5. The popup's pointer path is mapped and treated as a menu.
6. The RmlUi touch menu and the menu pad are deleted.

Each step closes when its gap lines are gone from the scan and the screen is
shown working in the product through its route.
