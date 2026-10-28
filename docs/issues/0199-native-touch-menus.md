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
| list | list box `onMouse` `0x005c0e10` (no scroll) | none on touch: `list_touch` takes it |
| footer token | `CMenuItemText::onMouse` `0x005c6490`, unless the menu's own `onMouse` acts on it (`x2::native::menu_handles_token`: the codex's and the team's `$MENU_DETAILS`) | none |
| popup | none: `CPopupDialog::onMouse` takes rows and footers; native on touch | -- |
| small target | the menu's own layout | every menu not native on touch: each clickable row and footer is under a tenth of the shorter client edge at 1280x720; native ones: their rows (step 4) |

Not reached by any route yet, so not in the list: the team menu's roster and a
hero's detail tabs (LT Details, RT Next hero, RB Drop and Assign), and `options`/`options_controller`
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
through `openmenu pda` at the boot map. Their rows are under a finger
(step 4).
Every popup is step 5's first screen, done: CPopupDialog has its mobile
layout on touch (cards of the game's bar art, a frame sized to its page, bar
buttons for the prompts, one tap to act, a drag to scroll), shown working by
`tools/live_case.py touch-popup` (13 of 13) and `touch-load` (9 of 9, eleven
saves, the last past the page's ten reached by drag through Load Game's own
window). The list boxes are step 5's second screen, done: CMenuItemListBox and
CMenuItemListCodex draw finger cards of the PDA's option art in their own box,
with the entry's icons and fitted text, a tap selecting through retail's own
press walk, a tap on the selected card accepting, a drag scrolling
(`src/native/list_touch.cpp`); the RmlUi shop, codex, review and danger room
views are deleted. Shown by `tools/live_case.py touch-shop`, `touch-stash`,
`touch-codex`, `touch-review` and `touch-danger-room`. The footers of every
menu native on touch are step 5's third screen, done: each `$MENU_*` prompt is
a finger button of the option art with its words and no keycap, and the tabs
of the shop, stash, review and danger room get finger areas; a press on either
is delivered at the item, so the menu's own handlers act (the codex's Details
included). Shown by the same cases tapping each button outside the game's own
box, and by `touch-menu` leaving Options by its Back button. The online region
list and the world map are native on touch and their RmlUi views are deleted:
the world map is the game's own screen with finger areas on its enabled act
tabs and point rows, shown by `touch-worldmap` (an act tap opening act 2, a tap
on the focused Avalon travelling, Back) and `touch-region`. The team menu is
the last screen the RmlUi touch menu replaces.
