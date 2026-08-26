---
id: 104
title: Escape or Start does not skip gameplay-authored camera and conversation cutscenes
status: resolved
symptom: Gameplay-authored in-engine cutscenes cannot be skipped with keyboard Escape or controller Start
tags: input,cutscene,conversation,scripts
created: 2026-08-22
updated: 2026-08-27
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

### Reopened (2026-08-25)
Regressed in 82bdf13: the new conversation waittimed override accelerated retail actor/movement waits after Escape. The user run recorded physical DIK Escape and first-conversation 0x13->0x18, then adjacent 0x18->0x10/no line (issue #83 signature). Remove the override and re-run the live Escape proof before resolving.

### Resolved (2026-08-25)

The 82bdf13 waittimed override is removed (its acceleration let the adjacent
conversation run before its actor/movement prerequisites and reproduced issue
#83's no-line signature). Live re-verification on the rebuilt tree,
tools/live_case.py cutscene-skip, 7/7: one Escape produced one request and
five retail response advances, nightcrawler_spawn and conv_0020b_end launched,
the adjacent conversation started VISIBLE with a selected line (0x18 -> 0x13,
line 0x41 -> 0x40 -- the issue #83 signature absent), controls unlocked, and
the latch retired with 'completed after control unlock 1'. The wiring audit
rejects any reintroduced 0x004d9130 override.

### Extended (2026-08-26): the scripted waits between records now skip too

USER, 2026-08-26: the previous resolution "just made it skip conversations in
the cutscenes" -- correct: the latch fast-forwarded dialogue records, but the
cutscene's scripted time (the tutorial teleport cutaway is playanim +
waittimed(2.0) + fx + waittimed(0.5); the cleanup script holds another 2.5s of
waits before it unlocks controls) still played out, and the removed 82bdf13
attempt had made acceleration independent of Escape. The user also ruled: the
game's own scripts are never modified; the cutscene PLAYER is what the port
drives.

Resolution: the script scheduler's insert (FUN_004d6a00 -- the choke point
both waittimed and the script VM reach, never anything else) clamps wait
deadlines to a 0.10s floor while the skip latch holds. The floor is the
difference from 82bdf13: a zero floor let the next startConversation race the
conversation manager's ending unwind and reproduced the issue #83 no-line
signature. The scope is the latch itself (armed at the authored boundary by
Escape/Start or by the boot-Continue resume; retired at control unlock) plus
a 30s runaway timeout -- a cutscene is a CHAIN of script contexts
(tutorial1 -> nightcrawler_spawn -> nightcrawler_walk -> cleanup), so the
first attempt's single owner-context claim refused the chain's own waits as
foreign (1 of 10 shortened); the live run under the latch-only scope
shortened 6 of 10 and cut the spawn-to-adjacent-conversation gap from 428
frames to 53. waittimed itself (0x004d9130) remains unmodified and the wiring
audit still rejects any override of it; the audit now requires the floor, the
clamp and the scope instead of the old "untouched" string.

Verification: tools/live_case.py cutscene-skip 7/7, cutscene-skip-early 7/7
(press during the camera-only pan), boot-continue 13/13 (resume sequence and
clamp together, no seen-bit collision); check_conversation_skip_wiring
--selftest 12 broken chains rejected.

### Note (2026-08-27)
Superseded 2026-08-27 by issue #122. The conversation latch and FUN_004d6a00 deadline floor described in this issue were attached below the owner and are deleted. The current resolution is the cutscene player documented in docs/RE/cutscene_player.md and verified by C247/C263.
