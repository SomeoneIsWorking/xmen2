---
id: 199
title: Native touch menus: the game's own menus answer a finger, nothing drawn over them
status: open
symptom: "Touch menus are a port-styled RmlUi panel over the retail menu, and the other menus get a drawn controller pad"
state_items: S025, S020
tags: touch,menus,ui
created: 2026-10-27
updated: 2026-10-29
---

# 0199 — Native touch menus

## Goal

On touch, the retail menu is the UI: drawn by the game in its own art and style,
and touched directly. Nothing is drawn over a menu, neither the RmlUi touch menu
nor the menu pad. Anything touch needs that the game lacks is drawn by the game's
renderer from the player's install.

## Work list

`tools/scan.py` opens every menu its console can open (`boot.mode=menu`, then
`X2_BOOT_MAP=act2/jungle/jungle1`, a tap answering the level's opening line,
and `openmenu <name>`) with touch forced on, and prints the run-end `menu touch gap:` lines from `x2::input::MenuTouchGapReport`
(`src/input/menu_touch_gaps.cpp`). Each kind names the guest function that
refuses the finger:

| kind | refused by | seen on |
|---|---|---|
| row without command | `CMenuItem::onMouse` `0x005bc1b0` (no `usecmd`) | none since `menu_item_tap` |
| step | `CMenu::onMouse` `0x005ae0a0` (no left/right) | none: Options' volume rows were a misreading, `CMenuOptions::onMouse` `0x005cb5f0` steps its bars |
| list | list box `onMouse` `0x005c0e10` (no scroll) | none on touch: `list_touch` takes it |
| footer token | `CMenuItemText::onMouse` `0x005c6490`, unless the menu's own `onMouse` acts on it (`x2::native::menu_handles_token`: the codex's and the team's `$MENU_DETAILS`) | none |
| popup | none: `CPopupDialog::onMouse` takes rows and footers; native on touch | -- |
| small target | the menu's own layout | every menu not native on touch: each clickable row and footer is under a tenth of the shorter client edge at 1280x720; not on a native item with a finger area (`x2::native::text_has_finger_area`) |

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

Main, Options and the PDA are native on touch (`menu_is_native_on_touch`)
and their rows are finger targets, step 4, done. Retail's main and PDA rows
are 14 scene units tall at a 36-unit pitch (0.94 of the 38.4-unit finger at
720p); each row label gets a finger area as wide as its bar and a finger tall,
delivered at the box its menu tests, the main menu's Quit included. Options'
settings page is redesigned for touch: the retail panel (rows 18 to 23 units
apart) is hidden and the nine settings are finger cards of the PDA option art
in a column a drag scrolls, five shown at a 1.15-finger pitch, with the game's
own label, value and bar drawn in each; a tap toggles, a tap or slide on a
volume card steps retail's bar to the finger (`src/native/options_touch.cpp`,
[touch play](../touch-play.md)). Shown by `tools/live_case.py touch-menu`
(28 of 28: every main row tapped at its edge outside the game's box, Options'
cards, a toggle, both volumes, a scroll, Back, Quit) and `touch-pda` (13 of 13:
every PDA row tapped at its edge, each judged by the page, menu or popup it
opened). Options' controls page keeps its retail layout.
Every popup is step 5's first screen, done: CPopupDialog has its mobile
layout on touch (cards of the game's bar art, a frame sized to its page, bar
buttons for the prompts, one tap to act, a drag to scroll), shown working by
`tools/live_case.py touch-popup` (13 of 13) and `touch-load` (9 of 9, eleven
saves, the last past the page's ten reached by drag through Load Game's own
window). A message popup's text, a tutorial Game Tip's whole message, is
wrapped across the grown frame and sizes it, and a popup up in gameplay, with
no menu under it, has no pad over it: `touch-game-tip` (17/17 at 1096x720,
the tablet's aspect) and `touch-game-tip-720` (17/17). The list boxes are step 5's second screen, done: CMenuItemListBox and
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
on the focused Avalon travelling, Back) and `touch-region`.

The team menu is native on touch and the RmlUi touch menu is deleted (step 6's
first half). The party and roster cards are retail's and finger-sized; the
detail tabs put their controls in a finger column left of the panel, drawn by
the tab's own items (`src/native/team_touch.cpp`): stat and ai cards that
select by walking the game's focus with pointer hovers and spend only from
their + and - (or a tap on the selected ai card), the selected skill's card
with -, + and Assign, the power diamond's Left, Right and Bottom slots laid
out as the diamond holds them and Cancel while assigning, Prev and Next hero,
and a Details view a tap holds open. The game's skill rows are a finger
apart. The gear tab's slots and pieces take turns in the slots' whole box as
finger cards, the three slots at once and the pieces four at a time, CMenuTeam's
onMouse handing a message on them to the list drawn there. A footer row whose
buttons would touch (the skills tab's five after a rank is added) splits its
span into equal buttons. Shown by `touch-team` (12/12),
`touch-team-stats` (21/21), `touch-team-skills` (23/23), `touch-team-gear`
(22/22), `touch-team-ai` (18/18), `touch-team-assign` (27/27),
`touch-team-skill-details` (14/14) and `touch-roster` (15/15).

Conversations are native on touch: the speaker and line readable in the
game's panel and each response a finger-tall card of `ui/hud/dialog_select`
that a tap picks (`src/native/conversation_touch.cpp`, see
[touch play](../touch-play.md)); no pad is drawn over them. Shown by
`touch-conversation`, `-4x3`, `-16x9` and `-20x9`.

A cinematic draws no pad: the pad goes only over a readable retail menu with
no touch path (`x2::native::TouchMenuSource::refresh`), where it had gone
over every control-locked stretch with no menu up, the cinematic gaps between
a conversation's lines included. Shown by `touch-cinematic` (CINEMATIC_COUNT).
The automap is native on touch: a drag keeps the ground under the finger, a
held finger pans nothing, the Rotate prompt is two hold buttons
(`src/native/automap_touch.cpp`), shown by `touch-automap`, `-4x3`, `-tablet`
and `-20x9` (AUTOMAP_COUNT). The text entry is native on touch and typed on
the platform's keyboard, a phone's screen keyboard: the port had delivered no
`WM_CHAR`, the only way characters reach it
(`src/native/text_entry_input.cpp`), shown by `touch-text-entry`, `-4x3`,
`-tablet` and `-20x9` (TEXT_COUNT). The online hero list is native on touch
through the list layout, shown by `touch-hero-list`, `-4x3`, `-tablet` and
`-20x9` (HERO_COUNT).

The review's image viewer, the credits, the controller page and a personal
file are native on touch (`src/native/page_touch.cpp`, see
[touch play](../touch-play.md)): the viewer's Back, Previous and Next are
finger buttons and a swipe pages it, a tap ends the credits, the controller
page's Back, which retail draws nowhere, is a finger button in its empty
description line, and a personal file's Back is a finger button. Shown by
`touch-image-viewer`, `touch-credits`, `touch-controller-page` and
`touch-personal`, each also `-4x3`, `-tablet` and `-20x9`. Retail never
opens the controller page or a personal file ([menus](../RE/menus.md)).

The online lobby menus are native on touch: CMenuOnline, CMenuCampaignLobby,
CMenuGameOptions, CMenuPlayerGameOptions, CMenuHost, CMenuJoin,
CMenuGamesList and CMenuPlayersList. Their own onMouse walks the focus to
the row under the pointer and accepts on the focused one, so each row is a
finger area at its raw box and the footers are finger buttons. Shown by
`touch-lobby-host` (play online, Ready, Host Game, Post Game, Leave Room)
and `touch-lobby-join` (Join Game, Search, the games list's Back, the
players list, the join room under its popup), each also `-4x3`, `-tablet`
and `-20x9`. The join room's own Ready and Leave Room are finger areas but
were not tapped: with no session it closes with its "no longer available"
popup, and a session needs a second running game. `players` is
`PLAYERS_MENU`, which the PC factory does not build: `openmenu players`
gives a plain `CMenu` that draws nothing, and no PC string opens it.

The menu pad is not deleted, because CMenuSebas (Advanced Options) still
draws it. The way in is touchable: Options' footer `Advanced...` is a finger
button posting `MENU_OK`, the Start its update opens Advanced Options on
(`touch-advanced-options`), and `GET /menu/action?name=ok` is the scan's
route to it. But the screen is a second GUI, not CMenu items
([menus](../RE/menus.md#advanced-options-cmenusebas)): `FUN_0061dc10` builds
its widgets in client pixels and `FUN_00621ea0` hit-tests the pointer
against them. A tap selects a tab and puts a binding cell into capture, but
its rows (24 pixels apart at 720p) and tabs are under a finger, `Esc Back`
is a label, and a capture waits for a key; Escape, the pad's B and a tap at
Accept each left it up, so no way out of it is established. Its touch layout needs that GUI's widget classes read
(rectangle, the table's row height and text size, the capture state) so a
port layout can place them as finger cards and give Back and Cancel a
button; until then the pad stays the touch path over it.

The team menu's open items are closed: footer buttons disjoint with five
footers (`touch-team-skills` and its `-4x3`, `-tablet` and `-20x9` windows),
slot buttons that name and mirror the diamond (`touch-team-assign`, each
slot button putting the skill in its slot), gear cards filling the panel
(`touch-team-gear`: three slots, then both pieces, a tap equipping), finger
skill rows (`touch-team-skills`: pitch 39 against a 38.4 finger). A popup's
frame stands on the screen's bottom edge, so the dialog art's prompt strip
below it is off the screen (`touch-game-tip`, `-720`).
