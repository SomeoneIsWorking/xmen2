---
id: 188
title: LAN multiplayer has no route without GameSpy's servers
status: open
symptom: with the Play Online row enabled, the retail online menu stops at "Online play is temporarily unavailable"; the PC build has no LAN mode to fall back to
state_items: S023
tags: network,multiplayer,gamespy,winsock
created: 2026-09-24
updated: 2026-09-24
---

# 0188 — LAN multiplayer has no route without GameSpy's servers

## What the retail PC build does

Read from XMen2.exe and observed on the native port with the row enabled:

1. **Main menu.** `label_option09` (*Play Online*) opens the `online` menu
   (`FUN_005c9640`). Our Continue feature disables that row when there is no
   save, and replaces it with Continue when there is one
   (`src/save/continue_policy.c`). That is deliberate, because the row leads
   nowhere.
2. **Online menu show** (`FUN_005caff0`). The menu writes the local IP into
   `data_networkfile` and then **disables** `text_networkset` and
   `data_networkfile`. On PC there is no network-configuration choice, so no
   LAN/Internet switch exists.
3. **Availability check** (`FUN_00641b50` start, `FUN_00641d00` think). This is
   GameSpy's `GSIStartAvailableCheck("xmenlegpc")`, a UDP query to
   `xmenlegpc.available.gamespy.com:27900`. It fails open: an unresolved name,
   or two silent 2 s retries, both mean *available*. That name no longer
   resolves, so the check passes.
4. **Peer login** (`FUN_00606200`). This creates a GameSpy Peer object (secret
   key `47uQsy`) and connects it to the Peer chat service. The connect callback
   (`LAB_00605d90`, message 0xfba at 0x605e28) is what shows *"Online play is
   temporarily unavailable"*. Nothing past this point was reached.
5. **Lobby.** After login the flow is the `campaign_lobby` menu (Host / Join),
   then `games_list` (Server Browser) or `host`. The exe links QR2 query
   reporting (`localip%d`, `\hostname\gamemode`), the Server Browser
   (`%s.master.gamespy.com`), and NAT negotiation (`natneg1/2.gamespy.com`).

The Play Online path also needed two host fixes, both landed with this issue:
real WS2_32 sockets, and per-thread static TLS. The first GameSpy thread read
`fs:[0x2c]` at 0x63efb0 and faulted on the zero pointer.

## Designs

**A. A local GameSpy-compatible lobby service in the host instance.** A
normally started port serves the minimum of Peer chat (IRC with GameSpy's
peerchat cipher), master/Server Browser, and QR2 on the LAN. Every instance
finds hosts by UDP broadcast of our own, and our `gethostbyname` answers the
`*.gamespy.com` names with the chosen host. The game's unmodified lobby,
join, and session code then runs as shipped. Cost: three GameSpy protocols
and their ciphers, and a policy for which instance serves.

**B. Drive the game's session layer below GameSpy.** Recover the calls that
the lobby's Host and Join end in: the game's own transport, which carries
gameplay peer to peer. Then call them directly. Auto-host on boot, and the
main-menu scan, become our own LAN discovery feeding a native list that joins
by address. Cost: RE of the session and transport layer and of what the lobby
state machine sets up before it hands over. It is unknown how much of that
state lives in GameSpy objects.

Both are multi-session efforts. Neither has started.

## Falsifier

Two isolated instances on one LAN (separate profiles and control ports).
Instance A starts normally and hosts. Instance B's main menu lists A within a
few seconds of showing, joins, and both reach the same level with two
controllable heroes. A run in which B only *lists* A proves discovery, not
multiplayer.
