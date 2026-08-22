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

`conversation_cutscene_skip.c` classifies the actual authored boundary from a
visible conversation; parsed `noReturnToGameCamAtEnd` camera ownership and the
live retail `lockControls` deadline retain that identity across preparation and
locked gaps. Action 20 (Escape/Start through Pause row 17) latches there and
advances one deterministic response per frame through the existing conversation
`chooseResponse` vtable slot. A
genuine response branch cancels the latch instead of choosing for the player.
A locked interval keeps the latch across adjacent conversation records in one
authored sequence; it clears when control returns.

The live input probe reports action 20, camera ownership, control lock,
response cardinality, latch state, dispatch count, blocked count, and ignored
requests. The pure policy regression proves non-authored input is ignored,
waiting/adjacent segments retain the latch, deterministic records advance, and
choices/unreadable state block.

Verification: 18 policy checks; the production wiring audit plus four negative
discriminators; direct `-Wall -Wextra -Werror` syntax checks of the production
chain; Python lint; and the structure gate pass. A full CMake reconfigure is
currently refused by its intended stale-generated-code guard after concurrent
native override changes; no game was launched for this fix.

### Resolution (2026-08-22)
Resolved at the production conversation boundary: action 20 latches on a visible authored conversation (with camera/control state covering preparation and locked gaps), advances only deterministic records through retail chooseResponse/applyResponse, and blocks at choices so cleanup scripts remain authoritative. Policy, wiring, syntax, lint, and structure gates pass; bounded live scene remains follow-up validation.
