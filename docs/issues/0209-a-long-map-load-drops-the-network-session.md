---
id: 209
title: a long map load drops the network session
status: open
symptom: "Both players load the tutorial map; 1 to 2 s after it appears the host reads 'You have lost communication with one of the other player(s)' and the client 'Player(s) have been dropped from the game.'"
state_items: S023
tags: lan,performance
created: 2026-11-12
updated: 2026-11-12
---

# 0209 — a long map load drops the network session

## Observed

Two players on one host in separate namespaces (`scratch/lan-pub/measure.py`,
friends by code, Host Game, Start Game). In 3 of 9 runs both dropped right
after `act0/tutorial/tutorial1` loaded: runs 6, 7 and 11, at 23:22:01,
23:35:26 and 00:06:06.8 UTC on 2026-11-11/12.

## Evidence

- The two games exchange 9-byte pings every 2 s from their main loops,
  through the movie and the first part of the load.
- The last step of the map load holds each main thread in one frame: in
  run 11, A for 4832 ms and B for 5575 ms (`gpu: frame ... took`), with no
  datagram in either direction (packet captures on both namespaces). Both
  frames ended within 13 ms of each other; the drop followed 1.8 s later.
- Every datagram either side sent reached the other's game port, before and
  after the silence (wire against loopback, second by second).
- Memory and CPU pressure stayed at zero through the stall (`/proc/pressure`
  sampled every 0.5 s). The heartbeat shows MAIN running guest code.
- Runs whose longest load frame was 3.5 s or less (run 9: 3.5 s, 2.0 s of
  silence; run 10: 3.1 s) kept the session. The path does not decide it:
  direct, lab relay and public relay runs both kept and lost it.

## Cause

Not yet traced. The game's peer timeout runs through the main thread's map
load, which here outlasts it. The next step is to find the timeout
(the routine that raises the lost-communication popup and the clock it
compares) and what the load does in its multi-second frame.
