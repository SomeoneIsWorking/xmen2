---
id: 190
title: a menu could not be left by touch
status: resolved
symptom: on the phone the World Map could not be closed, and touch outlines sat on the level names "Sanctuary" and "Grand Hall" instead of on the footer's Back and Go
state_items: S020
tags: touch,input,menu,ui,user-report
created: 2026-09-26
updated: 2026-09-26
---

# 0190 — a menu could not be left by touch

State items: S020 (platform-neutral touch play)

REPORTED BY THE USER, 2026-09-26, with a phone screenshot of the World Map:
"I'm stuck on this screen and can't close it and there are weird touch boxes
on it."

## Cause

Issue #180 made a footer prompt's words a control: the key cap came off, the
words' rectangle was retained, and it was published by the transform of the
draw that "submitted" it -- chosen by matching the draw's glyph count to the
prompt's. That is a length match, not identity, and its own header named the
limit. The World Map's level names `Sanctuary` and `Grand Hall` draw as many
glyphs as its footer prompts, so the footer's rectangles were published over
those rows, the footer itself got no control, and nothing on screen could
leave it.

## Resolution

The user's direction: no matcher. The retail menus are complete for a
controller, as on the Xbox, so touch play now draws one on every screen that
is not gameplay -- d-pad, A/B/X/Y, LB/RB -- through the same virtual pad as
the gameplay controls, and the footers name that pad's buttons in the shared
Xbox glyphs (`src/input/touch_menu_controls.cpp`,
`x2_layout_build_menu`). The prompt rewrite, its action-label registry, the
keyboard injection it pressed through, `/prompts`, and the synthetic pad's
exclusion from prompt naming are removed. A finger that begins off the pad is
still the retail pointer. Touch-native menus that replace the retail ones are
later work.

Evidence: `tools/live_case.py menu-pad`, 12 of 12 -- five d-pad Downs and A
open Options, B returns to the main menu (menu column 11.5 from the main menu
against 49.3 from Options). `test_touch_runtime` covers both classes of
finger and the release of a held button when gameplay takes the screen;
`test_touch_layout` sweeps the pad over nine viewports and fails 18 checks
with B and X swapped.
