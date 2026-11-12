---
id: 203
title: Port settings live in the game's own Options screen; render resolution defaults to native
status: done
state_items: []
tags: options,menus,settings,rmlui,resolution,text,touch
created: 2026-11-03
updated: 2026-11-12
---

## The request

"default should be native then I should be able to change from options, maybe
we also move RmlUI options to game's own options"

"You can remove advanced and put resolution options somewhere here on this
screen like on the right", "And the other options too", "We can move
everything out of RmlUI except like debugging idk"

"There are also other problems like main menu font size and options items font
sizes are different"

## Stage 1: native render resolution (landed)

`video.resolution` (`Settings::render_lines`) is `native` or a height.
`src/presentation/render_size.cpp:render_size_resolve` is the one rule that
turns it into the render size (`Settings::width/height`):

- Native where the output is the display (`output_is_display`: Android/iOS
  always, desktop borderless and fullscreen): the live window's pixel size, or
  the display's before the window exists.
- Native in a desktop window: the window's stored size, unchanged behaviour.
- A preset: that many lines, the width in the screen's shape.

The seed publishes the resolved size into retail `Display\Resolution`; the
adapter already enumerates the published mode and reports the real desktop,
so the retail legality pass accepts it. A surface change after the device
exists (`SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED`, or a window the host gave at
another size than predicted) is followed live by
`live_resolution_follow_surface`. Where the host's window is the display
(`host_window_fills_display`) there is no window mode: `window_settings_apply`
leaves geometry to the host, and the settings UI has no Window mode row.

The choice is the Resolution row of Options (stage 2).

## How retail Options is built

`options` is an `OPTIONS_MENU` XMLB of 73 items (cap `0xaf` = 175 at
`CMenu+0x1608`, `docs/RE/menus.md`): ten text rows, volume bars with
`leftcmd`/`rightcmd` (`setdecrement`/`setincrement`), the rest `usecmd`, each
value a separate item paired by its command's last word, values backed by
gamevars (registry `0x7acad0`). `CMenuOptions::onMouse` (`0x005cb5f0`) hit-tests
its own named items; `menu+0x18d8` is the page (0 settings, else controls).
Its update (`FUN_005cbdf0`) opens `sebas` (Advanced Options) on `MENU_OK`.
On touch, `src/native/options_touch.cpp` hides the panel art and draws the
nine settings as cards of `ui/models/m_pda_option_screen` /
`m_pda_option_focus`, a tap being the retail click on the row.

A retail menu XMLB is derived from the player's install at first open
(`src/native/derived_menus.cpp`) and served in place of the shipped file.

## Stage 2: the right-hand column

1. **Derive `options`** with a second column of rows in the right half: each
   a label item and a value item whose `usecmd` is a port command registered
   beside the retail ones (`src/native/options_menu.cpp:PORT_COMMANDS`).
   Values are port text written into the value item by one owner
   (`src/native/port_option_rows.{hpp,cpp}`, new), never gamevars.
2. **One row table** (`port_option_rows`): id, label, value formatter, step
   function over `Settings`, apply (live where the owner supports it). It is
   the only place a port setting becomes a row; the commands, the value text
   and the touch cards all read it.
3. **Drawing.** `options.igb` has no nodes for new rows, so the column is
   drawn with the same option-card art the touch layout uses, on desktop and
   touch alike; `options_touch.cpp`'s fixed nine-entry table becomes the
   retail half of a data-driven list and `src/input/options_touch_layout`
   lays out both columns.
4. **Rows, in order of landing:** Resolution; Display (window mode where it
   exists, dynamic shadows, shadow quality); Gameplay (extraction revive,
   boot mode); HUD (layout, vitals, potions, portraits, edge spacing); Touch
   controls.
5. **Sub-pages** for Controls (keyboard owners, controller assignment) and
   Keyboard bindings: a derived menu each (`port_controls`,
   `port_bindings`), opened by `openmenu` from a row so the retail menu stack
   owns Back. Binding capture keeps the existing capture owner
   (`src/input`), only its presentation moves.
6. **Advanced Options removed:** the `Advanced...` footer and the `MENU_OK`
   route to `sebas` go. Its settings fold in or are dropped: the Resolution
   slider is replaced by the Resolution row (the port's setting already wins
   at every launch); per-player key tables and Defaults 1-3 are replaced by
   the bindings sub-page and its Restore all; FSAA is dropped unless the D3D8
   host is shown to honour multisampling (check `d3d8_device.cpp`'s present
   parameters first).
7. **Deleting RmlUi documents:** each document is deleted in the change whose
   rows replace it: `display_settings_document`, `gameplay_settings_document`,
   `hud_settings_document`, `controller_assignment_rows`,
   `keyboard_bindings_document`, then `settings_document`,
   `settings_overlay_state`, the F2 toggle and the pause menu's
   `port_settings` row (`tools/make_port_pause_menu.py`). RmlUi keeps only
   what is not a setting: touch controls, notices (autosave, extraction
   revive prompt), the skip button, the first-run setup screen, and
   developer views.

## Stage 3: one text size per role

Today each piece of text takes its size from a different chain:

- Retail-drawn text (main-menu rows, Options title, Accept) is the font
  record's metrics times the AUTO scale `height/1080`
  (`ui_text_scale.cpp:ui_text_scale`), from whichever of the four records the
  item uses.
- Touch cards (`options_touch.cpp:draw_text`/`fit`, list, popup and
  conversation layouts) size text as `kCardText` (0.42) of a finger divided by
  the record's own line height, which cancels both the record and AUTO: the
  rows are a finger-relative size, the title and footer are not, hence oversized
  rows beside a tiny "OPTIONS" and "ACCEPT".
- The outline faces (`outline_text.cpp:Registry::derive`) follow the record and
  the retail pen; they are not the cause.
- "NORMAL" over the slant and "ON" clipped: `fit` measures against the card's
  full width less `kTextPad`, not the art's unslanted inner edge.

Fix at one owner, `src/native/menu_text.{hpp,cpp}`: a role (row, title,
footer) to scene-unit size rule that every port draw and the retail title/
footer draws read, row size equal to the main menu's retail row; and the card
text box from the art's measured inner bounds in `options_touch_layout`.
First close the gap the RE has: which XMLB attribute selects each item's font
record (`FUN_005ee590` reads `[box+0x2140c]`).

## Where it stands

Stage 2 is built: the derived menu (`options_menu_derived`), the rows table
(`port_option_rows`), the column (`options_port_column`, a single column
beside the retail panel rather than a two-column touch list), all rows of the
list above, and Advanced Options removed (`options_menu_no_advanced`). The rows
share two commands, `port_option_next`/`port_option_prev`, which step the
focused row: the retail registry holds 51 commands.

The Controls and Key Bindings rows open `port_controls` and `port_bindings`,
each derived from Options with only its frame, title, footer and Accept kept
(`derive_options_page`) and served Options' package (`derived_menus`). The
column lays a page's rows between its title and Accept and scrolls them. A
binding row's Change waits for the next host key (`src/input/binding_capture`).
The RmlUi settings overlay, its F2 toggle, the touch menu button and the pause
menu's `port_settings` row are deleted. RmlUi keeps the touch controls,
notices, the skip button, first-run setup and developer views.

Stage 3 is built as one rule: every row menu's text (main menu, Options, the
port column, the PDA's team and list cards) is the font record at scale 1,
shrunk only to fit (`menu_text::row_scale`); card text is inset by the art's
slant (`touch_cards::card_text_box`). Popups and conversations keep their
finger-relative size.

## Proof per stage

Stage 1: `tools/live_case.py native-resolution`. The sub-pages:
`options-pages` (both pages by keyboard and mouse, a binding captured, saved,
cancelled and restored). Each later stage: the touch
cases (`touch-menu`, `touch-pda`, `touch-conversation`, `touch-cluster`), an
Options case that steps every new row through the control channel and reads
the setting back from `x2native.conf`, and main-menu/Options shots at
2728x1264 and 1096x720 (`display.emulate`).
