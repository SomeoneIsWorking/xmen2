---
id: 206
title: a host with no campaign running cannot start a LAN game
status: resolved
symptom: "Start Game loads, the host falls back to its main menu behind an empty popup, the client reads 'The host has left the game.'"
state_items: S023
tags: lan,menus
created: 2026-11-11
updated: 2026-11-12
---

# 0206 — a host with no campaign running cannot start a LAN game

## Observed

A fresh profile at the online lobby was re-formed as a LAN lobby through
`/lan?host=1`. The director logged `the captured campaign is the hosted save
(mode 1)`, B joined, and Start Game loaded and fell back to the host's main
menu; B read "The host has left the game."

## Cause

The director's host role re-forms a running campaign: it serializes the
campaign the game owns and hosts that save
(`src/native/lan_session_director.cpp:install_campaign`). With no campaign
running there is nothing to serialize, and the hosted save loads nothing. The
player's own route never reaches it, since the coordinator re-forms only a
game that announces a campaign map. The control route `/lan?host=1` asked for
it with no such check.

A player on a fresh profile hosts through retail's own Host Game, Game
Options and Post Game. That path had two defects of its own, which kept the
two-player run from it:

- a tap on Host Game while Your code had the focus did nothing, because the
  lobby's retail onMouse walks the focus from its own two rows
  (`src/native/lobby_friend_cards.cpp`);
- a host room opened by a player was never announced, because the presence
  fact `hosting_lobby` was the director's script step rather than the host
  room being up (`src/native/lan_coordinator.cpp:read_facts`).

## Fix

- `/lan?host=1` refuses (409) while no campaign map is loaded.
- A press on Host Game or Join Game while a friend row has the focus first
  focuses that row, so retail's onMouse takes it as on the retail rows.
- `hosting_lobby` is true while this machine's host room is up, whoever
  opened it.

Proven 2026-11-12, by touch on both fresh profiles: A tapped Play Online,
Ready, Your code, Host Game, Post Game; B pasted the code and tapped
`Join fedora`; A tapped Start Game, and both loaded
`act0/tutorial/tutorial1` in one network game.
