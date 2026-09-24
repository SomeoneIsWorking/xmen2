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

## The retail LAN path needs no GameSpy server

The game's own Server Browser (`games_list`, CMenuGamesList vtable 0x0069f8b4,
show `FUN_005b9de0`) does not ask a master server. Its periodic task
(`LAB_00609080` → `FUN_00608f10`) broadcasts CNetPlayManager query 0x19 on the
game socket (port 5165) while its address list `DAT_00a3bc1c` is empty. A host
answers through the handlers `FUN_00609c80` registers (0x19 → `FUN_00608240`
→ `FUN_00607cd0`) with reply 0x18, which the client's `FUN_006090d0` adds to
the list at manager +0x2208 (count +0x2214). Selecting a row joins through
`FUN_0060a0a0`. Only the Peer login stood in front of it.

Landed, and observed with two isolated instances on one machine: A hosts from
Play Online → Host; B's Server Browser lists A, joins, both ready, A starts, and
the two heroes are controlled from their own instances. B quitting shows the
game's "Player(s) have been dropped from the game" on A, which continues.

- `lan_login.c` answers `peerConnect` (0x0062f070) through the game's own
  callback 0x00605d90 with the success branch, so no GameSpy name is resolved
  and the "temporarily unavailable" race is gone. Later Peer SDK calls decline
  on the Peer's clear connected flag.
- `winsock_resolve` gives Windows' `gethostbyname` answers: "localhost" is named
  after the machine, and the machine's own name lists its adapters with the
  default-route address first. `FUN_00615d30` learns the address it advertises
  that way; a POSIX resolver answered 127.0.1.1.
- A datagram socket bound to an adapter address is bound to INADDR_ANY on the
  host (Linux delivers no broadcast to a unicast-bound socket, Windows does),
  and `getsockname` reports the requested address.
- Continue no longer hides Play Online when there is no save.
- The host's co-op participation policy works in seats and translates them
  through the player→controller map, so it never evicts a network player
  ([co-op participation](../RE/co_op_participation.md)).

## Still open

- **Drop-in.** Once the host starts the game, `FUN_006097f0` (from 0x006135b4)
  runs `FUN_006074b0`, which unregisters the lobby handlers, so a new client's
  browser shows "No Games Found". Joining a running game needs the host to keep
  answering 0x19 and the engine to accept a mid-game join; neither is known yet.
- **Seamless entry.** Auto-hosting on a normal start and a main-menu list of
  LAN hosts are not built; today the player goes Play Online → Host or Join.
- **With a save,** Continue still replaces the Play Online row.
- **Network pause.** It waits for every player's Ready; the keyboard key that
  readies has not been identified.

## The transport

- **Transport** (`CNetModuleWin32`, vtable 0x006a4a88). One UDP socket,
  opened by slot 5 (`FUN_00616480`): bound to the module's port, retrying on
  another port when that one is in use (WSAEADDRINUSE), with SO_BROADCAST set
  by slot 1 and FIONBIO non-blocking. Slot 3 (`FUN_00616620`) is `sendto`.
  Slot 7 (`FUN_00616340`) drains `recvfrom` into 0x514-byte frames. A hook at
  +0x14708 lets GameSpy's QR2/NAT-negotiation claim packets on the same
  socket first. An unknown sender becomes a `CNetNode` (`FUN_0060eec0`), and
  the node lookup is `FUN_0060db70(address, port)`. All game sockets are in
  0x616xxx; everything at 0x63a000 and above is the GameSpy SDK.
- **Join by address.** `FUN_00609b80(address, port)` finds or creates the
  host's node, sends message 0x10010, and arms a timeout task. The NAT
  negotiation completion callback (`FUN_006050a0`) calls it with the peer
  address GameSpy brokered, and `FUN_0060a0a0` calls it when the manager's
  "client" flag (+0x3b3) is set. Otherwise `FUN_0060a0a0` starts serving
  (`FUN_006159f0`, `FUN_00615c60(1)`).
- **Unknown:** how much of the staging room (player list, ready state, the
  host's start that runs `startloadedonlinegame`) lives in GameSpy Peer
  objects rather than in `CNetPlayManager` / `CNetPlayer` messages. That
  decides whether B needs a Peer stand-in at all.

## Falsifier

Two isolated instances on one LAN (separate profiles and control ports).
Instance A starts normally and hosts. Instance B's main menu lists A within a
few seconds of showing, joins, and both reach the same level with two
controllable heroes. A run in which B only *lists* A proves discovery, not
multiplayer.
