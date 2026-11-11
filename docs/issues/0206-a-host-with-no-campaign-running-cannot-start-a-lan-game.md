---
id: 206
title: a host with no campaign running cannot start a LAN game
status: open
symptom: "Start Game loads, the host falls back to its main menu behind an empty popup, the client reads 'The host has left the game.'"
state_items: S023
tags: lan,menus
created: 2026-11-11
updated: 2026-11-11
---

# 0206 — a host with no campaign running cannot start a LAN game

## Observed

A fresh profile with no save, at the online lobby, re-formed as a LAN lobby
(`/lan?host=1`). The director pressed Host Game and Post Game and logged
`the captured campaign is the hosted save (mode 1)`. B joined
(`/lan?join=1`), both were Ready, and the director pressed Start Game:
`the lobby started the game`, `loading`, then the host's `main` menu with an
empty popup over it. B showed "The host has left the game." Run:
`scratch/lobby/two.py` on 2026-11-11.

The S023 proof hosted from a running tutorial, whose campaign the re-form
captured, and both players loaded it.

## Next

Find what Start Game loads when the captured campaign is the hosted save
and there is none, and what retail's own Host Game does for a player with no
save. Then decide whether the host needs a new game first or whether the
re-form must start one.
