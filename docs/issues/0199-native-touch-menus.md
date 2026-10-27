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
| row without command | `CMenuItem::onMouse` `0x005bc1b0` (no `usecmd`) | none since `menu_item_tap` |
| step | `CMenu::onMouse` `0x005ae0a0` (no left/right) | none: Options' volume rows were a misreading, `CMenuOptions::onMouse` `0x005cb5f0` steps its bars |
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
4. Row menus (main, Options, PDA) are scaled for touch so the smallest target
   is finger-sized.
5. Selector screens get a touch layout, because scaling them is not enough:
   the difficulty popup, Load Game, the shop and stash, and every other list
   box with the white selection rectangle (codex, review, danger room,
   trivia). A list becomes a short column of finger-tall rows drawn with the
   game's own row art and focus highlight, one tap picks an entry and a drag
   pages it, and the controller cursor is not drawn on touch. A menu is data
   (`UI/menus/<name>.xmlb` over its `.igb` art, loaded by `FUN_005ab130`), so
   the layout is a port-owned transform of the player's own file when it is
   loaded, using retail item types and art. No asset is copied and nothing is
   drawn over the menu. The popup's pointer path (`popup+0x7c`) is mapped first.
6. The RmlUi touch menu and the menu pad are deleted.

Each step closes when its gap lines are gone from the scan and the screen is
shown working in the product through its route.

## State

Main, Options and the PDA are native on touch (`menu_is_native_on_touch`):
`tools/live_case.py touch-menu` taps Options open from the main menu, lowers
the effects volume, toggles Combat Music and leaves by the Back footer, all on
the game's own screens; a PDA tap on Team Management opens the team menu
through `openmenu pda` at the boot map. Their footers still draw keyboard
keycaps (step 3), and their rows are under a finger (step 4).
The difficulty popup, Load Game and the shop are step 5.
