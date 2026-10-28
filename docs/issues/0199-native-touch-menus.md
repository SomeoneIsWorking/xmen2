---
id: 199
title: Native touch menus: the game's own menus answer a finger, nothing drawn over them
status: open
symptom: "Touch menus are a port-styled RmlUi panel over the retail menu, and the other menus get a drawn controller pad"
state_items: S025, S020
tags: touch,menus,ui
created: 2026-10-27
updated: 2026-10-28
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
| popup | none: `CPopupDialog::onMouse` takes rows and footers; native on touch | -- |
| small target | the menu's own layout | every menu: each clickable row, footer and list row is under a tenth of the shorter client edge at 1280x720 |

Not reached by any route yet, so not in the list: the team menu's roster and a
hero's detail tabs (LT Details, RT Next hero, RB Drop and Assign), the world
map's points (no unlocked point at the boot map), and `options`/`options_controller`
from the title menu (the scan opens them from a level).

## Steps

1. A finger is the retail pointer on every menu: down moves it, up clicks.
2. Rows without a command, steps and lists answer a tap at their owners above.
3. Footer tokens the game does not publish get their action at `CMenuItemText::onMouse`.
4. Row menus (main, Options, PDA) are scaled for touch so the smallest target
   is finger-sized.
5. Selector screens get a mobile layout, because scaling them is not enough:
   the difficulty popup, Load Game, the shop and stash, and every other list
   box with the white selection rectangle (codex, review, danger room,
   trivia). Widening the retail rows (the first popup pass) only spaces out
   the same small lines; on touch each selector is laid out the way a mobile
   game lays out its own screens, by one port-owned layout per screen kind,
   drawn by the game's renderer with the game's font, bar and frame art:
   - a large title; full-width cards for the entries, each the game's bar art
     dimmed with the entry text larger, the selected card at full brightness;
   - the card list fills its frame and scrolls by drag; one tap picks a card;
   - the footer actions are big buttons in the same art, not keycaps;
   - list boxes put the cards on one side and the detail panel on the other.
   The controller cursor is not drawn on touch, nothing is drawn over the
   menu and no asset is copied. The popup's pointer path (`popup+0x7c`) is
   mapped first.
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
The difficulty popup is step 5's first screen. Its first pass only widened the
retail rows to a finger's pitch (a tap on Easy selects it and a second tap,
short or held 0.6 s, starts the new game); the mobile layout replaces it. Touch reaches the game through its
own event callback at the frame boundary (`touch_pointer_dispatch.cpp`), not
the Win32 queue. Every popup is native on touch, with nothing drawn over it
(`tools/live_case.py touch-popup`, 11 of 11). The popup's mobile layout is next,
then Load Game (its window follows the page's visible count) and the shop.
