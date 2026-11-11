---
id: 205
title: the main menu's LAN Join row does nothing after a visit to Play Online
status: open
symptom: "Join <host> is listed and takes focus, but a tap or Return on it starts no join"
state_items: S023,S026
tags: lan,menus
created: 2026-11-11
updated: 2026-11-11
---

# 0205 — the main menu's LAN Join row does nothing after a visit to Play Online

## Observed

Two players on separate subnets (`scratch/lobby/two.py`). B went main menu →
Play Online → Ready → lobby, took A's code with Join by code, and backed out
to the main menu. A re-formed as a lobby (`/lan?host=1`), and B's main menu
listed `Join fedora` as its first row. A tap on the row focused it; a second
tap and then Return on the focused row did nothing. `port_lan_join` never ran:
no `lan: joining` and no `lan: Join chosen, but no LAN game is announced`
line. `/lan?join=1` from the same state joined A's room (session players 2).

In the S023 proof B had never opened Play Online, and Return on the Join row
joined.

## Known

- `CMenuMain::onMouse` (`0x005c9320`) on `0x202` over the focused row only
  sets the active player's accept (`0x00a09fa0[player]`, `0x00a09f54[player]
  = 4`), so tap and Return go the same way from there.
- `src/native/continue_runtime.cpp:apply_menu_plan` writes the row's command
  (`item+0x24`) and sets `ONLINE_COMPARATOR` to the empty string when the
  online row is dropped for Join.

## Next

Trace what the main menu's accept does with the focused row after Play
Online: whether the row's command at `+0x24` is still `port_lan_join` when
the accept runs, and what the online visit leaves in the menu mode
(`0x003298a8`) and the active player (`0x006f3920`).
