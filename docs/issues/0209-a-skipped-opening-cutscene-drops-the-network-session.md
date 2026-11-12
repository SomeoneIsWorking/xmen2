---
id: 209
title: a skipped opening cutscene drops the network session
status: resolved
symptom: "Both players load the tutorial map; 1 to 2 s after it appears the host reads 'You have lost communication with one of the other player(s)' and the client 'Player(s) have been dropped from the game.'"
state_items: S023
tags: lan,cutscene
created: 2026-11-12
updated: 2026-11-12
---

# 0209 — a skipped opening cutscene drops the network session

## Observed

Two players on one host in separate namespaces (`scratch/lan-pub/measure.py`,
friends by code, Host Game, Start Game). Both dropped right after
`act0/tutorial/tutorial1` loaded, 1 to 2 s after the map appeared, in 3 of
the first 9 runs and in 9 of 10 later runs. The script taps the cutscene's
Skip button on each side as soon as it shows. Paced and unpaced runs both
dropped. The map load's 5 s frame and its silence on the wire happened every
time; they were not the cause.

## Cause

The popup is not a timeout. `FUN_00614c80` compares the sync checksums the
two machines exchange every 30 ticks (`FUN_00614a40`). On a mismatch
`FUN_00614810` drops the minority peer: strings 4300 on the host and 4301 on
the client, through `FUN_006133a0` and `FUN_00612c40`. The checksums differed
at the first or a later check, by the camera singleton's vectors or by the
shared random generator's draw count. A's camera stayed on the cutscene's
spot four frames after B's had left it, and two AI thinks then ran in the
opposite order on each side.

`src/native/cutscene_player.cpp:x2_override_004a00d0` completed the tutorial's
opening `lockControls` sequence synchronously on a local skip, at that
machine's own tick and only there. Retail has no skip for such a sequence, so
in retail both worlds play it on the same ticks (docs/RE/cutscene_player.md,
Network sessions).

## Fix

While the session lists more than one player, the cutscene player neither
offers nor completes its skip. A briefing's skip is retail's own action 20
and is unchanged.

Proven 2026-11-12. Before the fix, three pairs that never tapped Skip kept the
session through 102 to 385 matching checksums, against 9 drops in 10 pairs that
tapped it. After it, three fast pairs were offered no Skip and kept the
session 90 s past the load. With the host pressing Continue through the
opening conversation, both players reached the same gameplay frame with the
session up. `tools/live_case.py cutscene-skip` still passes 11/11 in single
player.
