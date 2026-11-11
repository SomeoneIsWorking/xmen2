---
id: 205
title: the main menu's LAN Join row does nothing after a visit to Play Online
status: resolved
symptom: "Join <host> is listed and takes focus, but a tap or Return on it starts no join"
state_items: S023,S026
tags: lan,menus
created: 2026-11-11
updated: 2026-11-12
---

# 0205 — the main menu's LAN Join row does nothing after a visit to Play Online

## Observed

Two players on separate subnets (`scratch/lobby/two.py`). B went main menu →
Play Online → Ready → lobby, took A's code with Join by code, and backed out
to the main menu. B's main menu listed `Join fedora` once A hosted. A tap
focused the row; a second tap and Return on it did nothing, and
`port_lan_join` never ran. `/menu` read the row's command as empty. Options,
chosen the same way after the same trip, opened.

## Cause

`src/native/continue_runtime.cpp:apply_menu_plan` interned `port_lan_join`
(and Continue's `loadgame`) into the guest's string pool once and kept the
handle. A handle is the pool's id and a hash slot (`0x0041a460`); the pool
has no reference counts and is refilled when the front end reloads its
menus, so after the trip to Play Online the kept handle named an empty slot
and the row ran an empty command.

## Fix

`apply_menu_plan` interns both commands each time it applies the plan.
Interning an existing string returns its slot.

Proven 2026-11-12, by touch on both players: B, back from the online lobby,
tapped `Join fedora` once and joined A's host room (client 1, session players
2), and both loaded `act0/tutorial/tutorial1`.
