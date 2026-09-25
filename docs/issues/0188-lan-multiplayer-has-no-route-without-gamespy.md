---
id: 188
title: LAN multiplayer has no route without GameSpy's servers
status: open
symptom: with the Play Online row enabled, the retail online menu stops at "Online play is temporarily unavailable"; the PC build has no LAN mode to fall back to
state_items: S023
tags: network,multiplayer,gamespy,winsock
created: 2026-09-24
updated: 2026-09-25
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

- **Drop-in.** The engine has no join-in-progress. Start Game
  (`FUN_005bacd0`, the host menu's `text_startgame`) sends message 0x26 and runs
  `FUN_00609400`, which unregisters every lobby handler (0x14/0x19/0x1a/0x1e,
  0x2c, 0x18), so a new client's browser shows "No Games Found". 0x26 carries
  only the start mode: each peer builds its own world in `FUN_005f3c20`
  (`startFirstMission()`, the Danger Room, or `startloadedonlinegame`).

  What the engine *does* have is a hosted saved campaign, and it works on the
  port. Observed: A picks Game Type → Load Saved Campaign in Game Options, posts;
  B's browser lists "Saved Campaign [1/4]", joins, readies; A starts and both
  load the save's scene. The pieces:
  - `FUN_00608260(save, flag)` copies a 0x2fc00-byte save into the net manager
    (+0x2250) and marks the session loaded (+0x3e1). The load-game menu calls it
    when the online manager is active (0x004aed57).
  - The host streams it with message 0x47 (header) and 0x48 (200-byte chunks);
    the client's `FUN_00608630` reassembles it and acknowledges with 0x4a.
  - `startloadedonlinegame` → `FUN_00606fb0` loads +0x2250 on every peer.
  - The port's autosave already produces that exact buffer mid-game: game
    vtable 0x208 serializes the running campaign (`autosave_runtime.c`).

  So drop-in is a re-formed session: the host serializes the running campaign,
  hosts it as a saved campaign, the joiner receives it, and every peer reloads
  at that state. A join therefore costs everyone one load; no route adds a
  player to a level already loaded.

  That re-form is built and observed (`x2::lan::SessionDirector`,
  `src/native/lan_session_director.cpp`). It drives the retail menus by
  focusing a named item and delivering one accept, and it reaches the front end
  with the console's own queued `mainmenuexit 1`. Host: capture → main →
  online → Ready → Host Game → install the capture as the hosted save (the
  load-completion sequence at 0x004aed10) → Post Game → Start Game once every
  player is Ready. Join: main → online → Ready → Join Game → the search menu
  (`player_game_options`, whose Search item is named `text_mapname`) → the
  first listed game → Ready (bit 2 of the local player's +0x1c, player from
  `FUN_006111f0`) → wait for the start. Observed: A mid-campaign with
  `/lan?host=1`, B at its main menu with `/lan?join=1`; both loaded the same
  scene and showed the same dialogue. The presence service below triggers both.
- **Seamless entry** is built on a presence service of the port's own (UDP
  5166, broadcast, `src/net/`), since the retail lobby is only reachable
  through menus. Each instance announces once a second what it is doing:
  playing a campaign map alone, holding an open lobby, or hosting a network
  game (a client in someone else's game stays silent). A peer expires after
  3.5 s. The main menu puts a "Join <host>" row first (`continue_policy.c`;
  with Continue also present, Review then Play Online give way). Choosing it
  sends JoinRequest until that host announces its lobby, then runs the
  director's join script. The host answers the first request by re-forming
  for everyone present plus every distinct requester; a request that arrives
  while the re-form is under way raises the count
  (`x2::lan::Coordinator::answer_join_requests`); before that fix, two
  machines asking in different polls produced a lobby for two that started
  without the second. The host starts once that
  many players are Ready, or after 45 s with those who are.

  Leaving a network game blanks the host-info block's version (session
  +0x3e2), and a browser hides a reply whose version differs from its own
  (`FUN_006090d0` clears the entry's compatible flag). The host script
  therefore rebuilds the block with `FUN_006156d0`, as a first host has it,
  before it opens the online menu.

  Observed: A playing single player, B's menu row joins it and both load the
  same scene; C then drops in and A re-forms around the running campaign.
  A client whose host re-forms loses it mid-level. The session's disconnect
  handler runs `lostconnectdialog` inline (console slot 0x18), so the dialog
  (`FUN_005f2220`, string 0x110c) is open before the port sees the empty
  session. Yes and Back continue in single player (Back runs Yes's script,
  empty or `dangerRoomEndMission()`); No runs `mainMenuExit()`. The director
  focuses the popup option whose script is `mainMenuExit()` (popup slot 0x34)
  and accepts it, so the dialog closes through its own handler, then follows
  the host back through its lobby.
- **Third and fourth players** need GameSpy's NAT negotiation, and the port
  answers it on the LAN. The session links every client to every other one:
  when a player joins a lobby that already has a client, the host sends the
  newcomer message 0x21 and each client already in it message 0x22, both
  carrying one cookie (`FUN_00605870`). Each side then calls
  `NNBeginNegotiationWithSocket` (0x0063b970) so natneg*.gamespy.com can tell
  it the other's address, and the completion callback `FUN_006050a0` links the
  two (`FUN_00607210`: the peer's node, then message 0x23). Those names no
  longer resolve, so Begin failed at once; the game ignores its result, and
  the host gave up after 15 s ("Unable to add player 3. Attempting to try
  again..."). Two players never negotiate, which is why they always worked.

  `x2::lan::NatNegotiation` overrides Begin and `NNCancel` (0x0063ba10). Each
  side broadcasts {cookie, client index, its game socket's endpoint} on the
  presence port (`RendezvousTable`), and completes through the game's
  callback with nr_success and the partner's sockaddr_in, or with
  nr_deadbeatpartner after 10 s without one. Observed: three fresh instances,
  B and C pressing Join together; both sides paired within 5 ms, the host
  added players 2 and 3, all three Readied and loaded the same scene.

  The clients that crashed after a failed add called a GameSpy SDK
  negotiator's completion callback that was never set. With the servers
  unresolvable, the real Begin allocated a negotiator (`FUN_0063b4f0`, zeroed)
  and returned ne_dnserror before storing its callbacks; the SDK's think
  (`FUN_0063be00`) later ran out of init retries and called `param_1[0xe]`,
  address 0. Reproduced by having the override decline without running the
  SDK: the add then fails and both clients carry on, because no negotiator
  exists. Since the port answers Begin, the SDK never allocates one.
- **Difficulty and the host's own apply.** The lobby labelled every re-formed
  campaign Easy. Session `+0x3df` is the difficulty (lobby text `0x867` +
  value, `FUN_005b8a50`), read from the owner's `+0x60c` (slot 0x268), and it
  was 1 before the host applied its save and 0 after. The snapshot's
  `+0x2fc00` is the stream cursor, not a self pointer: the serializer left it
  at the end of the campaign, and apply (slot 0x20c) read the zeroed tail --
  the owner block holding the difficulty and everything after it. Retail
  applies a buffer read from disk, cursor at its start; `FUN_00608260` rewinds
  only the copy it hosts, so clients were right and only the host was wrong.
  `CampaignSnapshot` now rewinds a finished capture. A Normal campaign now
  logs mode 1 and hosts as "Saved Campaign (Normal)".
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
