---
id: 208
title: GET /menu?items=all reports a stale focused item
status: open
symptom: "focused_row in /menu?items=all stayed on New Game while /menu, read at the same time, followed the pad down the rows"
state_items:
tags: tooling,menus
created: 2026-11-12
updated: 2026-11-12
---

# 0208 — GET /menu?items=all reports a stale focused item

## Observed

`scratch/lobby/host.py` read focus from `/menu?items=all` and saw
`label_option04` after every d-pad press. Switched to `/menu`, the same run
followed the focus through all six rows. On pages `/menu` has no rows for
(`game_options`, `host`), the items' `focused` flags were right.

## Next

Compare how `src/native/control_menu_route.cpp:menu_json` fills
`focused_row` with and without `all_items`.
