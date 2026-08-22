---
id: 104
title: Escape or Start does not skip gameplay-authored camera and conversation cutscenes
status: resolved
symptom: Gameplay-authored in-engine cutscenes cannot be skipped with keyboard Escape or controller Start
tags: input,cutscene,conversation,scripts
created: 2026-08-22
updated: 2026-08-22
---

## Root cause

The earlier action-publication proof covered the wrong subsystem. Retail action
20 is consumed by `cinematicStart`, but the shipped script corpus uses that
command for mission briefings. Gameplay-authored scenes instead call
`lockControls`, animate the camera, and enter `startConversation`; the retail
conversation update consumed only Accept action 4.

Clearing its visible/ending flags would be corrupt: `FUN_0045cde0`
(`applyResponse`) owns the path that launches `scriptFile` and
`chosenScriptFile` on every exit. Those authored scripts spawn characters,
advance quests, unlock controls, reset the camera, and load zones.

## Resolution

`conversation_cutscene_skip.c` classifies the actual authored boundary from
parsed `noReturnToGameCamAtEnd` camera ownership or the live retail
`lockControls` deadline. Visibility alone is presentation state and cannot make
ordinary dialogue skippable. Action 20 (Escape/Start through Pause row 17)
latches at that authored boundary and
advances one deterministic response per frame through the existing conversation
`chooseResponse` vtable slot. A
genuine response branch cancels the latch instead of choosing for the player.
A locked interval keeps the latch across adjacent conversation records in one
authored sequence. The retail update's hidden and disabled early exits also
feed the production policy, so the latch clears when authored cleanup returns
control even though the final conversation record no longer reaches the input
gate.

The live input probe reports action 20, camera ownership, control lock,
response cardinality, latch state, dispatch count, blocked count, and ignored
requests. The pure production-classifier regression proves a visible,
single-response ordinary dialogue is ignored, while
waiting/adjacent segments retain the latch, deterministic records advance, and
choices/unreadable state block. C252 records that scope boundary.

Verification: 21 policy checks; the production wiring audit plus six negative
discriminators; a current `x2native` build; and focused policy/wiring tests.

The first bounded tutorial run also found and falsified C241's lifecycle
claim. Escape advanced the real response chain and launched its cleanup
scripts, but the latch stayed active because the hidden conversation update
returned before observing that controls had been restored. The inactive
early-exit observation fixes that root cause. On the rebuilt rerun, the live
probe began at authored/visible/camera-owned/controls-locked with one
deterministic response. One Escape request at frame 1026 produced five retail
response advances, launched `nightcrawler_spawn` at frame 1027 and
`conv_0020b_end` at frame 1238, then reported visible no, controls-locked no,
and the authored skip idle. This is the cleanup-preserving positive result in
C247.

The controller rerun used the production transient-assignment path rather than
special-casing the synthetic device. After assigning the session-only X2
Virtual Pad to Player 1, the probe reported Escape and Start on Pause row 17 in
all three master/working/menu banks. Start at frame 15121 produced one request
and five retail advances, launched `nightcrawler_spawn` at frame 15122,
`nightcrawler_walk` at frame 15183, the adjacent conversation at frame 15333,
and `conv_0020b_end` at frame 15335. By frame 15841 the conversation was hidden,
controls were unlocked, and the authored skip was idle. C251 records this
independent controller proof.

### Resolution (2026-08-22)
Resolved at the production conversation boundary: action 20 latches on a visible authored conversation (with camera/control state covering preparation and locked gaps), advances only deterministic records through retail chooseResponse/applyResponse, and blocks at choices so cleanup scripts remain authoritative. Escape and assigned-controller Start are independently bounded-live verified end to end, including cleanup and latch retirement (C247, C251).
