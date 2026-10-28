---
id: 200
title: touch-worldmap and touch-team-gear stop before reaching their menus
status: open
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

## Next

Bisect between the 2026-10-07 pass and `bcae6e5` with `--binary`, then trace
the first failing step. For the world map, start with what
`keyboard_into_gameplay` sees after the second `loadmap`. For the team gear,
start with whether `GET /menu` reports `popup` once CPopupDialog is native on
touch.
