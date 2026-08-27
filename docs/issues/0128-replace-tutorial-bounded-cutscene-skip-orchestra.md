---
id: 128
title: Replace tutorial-bounded cutscene skip orchestration with the full retail in-game cutscene player
status: resolved
symptom: The skip retained the central BehavEd context interpreter and suppressed DirectSound globally instead of owning the complete gameplay command player and its exact audio presentation seams
tags: cutscene,player,re,port,audio,user-report
created: 2026-08-27
updated: 2026-08-27
---

## User requirement

Port the in-game cutscene player fully. When that native player is asked to skip, it must complete the cutscene without playing its audio. Do not substitute a tutorial-specific control-lock reconstruction or a blanket audio-boundary interception for the player.

## Root cause

Gameplay cutscenes are general BehavEd control-lock graphs, not conversation
objects or one monolithic retail `CutscenePlayer`. Retail `004d8b30` is the
central command-graph interpreter used by the scheduler at `004d9640`; the
title's timed entity-event player at `004b2d70` is its second asynchronous
child. The separate CHud cinematic surface is used by briefing scripts and is
not the gameplay control-return owner.

The retained `004d8b30` body left the core player unported, while the broad
DirectSound gate guessed that every voice started during a synchronous host
interval belonged to the cutscene. The two non-dialogue sounds in the verified
tutorial are instead exact BehavEd `sound` commands handled by `004a7130`.

## Resolution

`behaved_context` now ports `004d8b30` fully and is shared by ordinary and
synchronous scheduling. `cutscene_player` composes it with the already-native
BehavEd scheduler and timed-event player until authored control release, leaving
post-release script work scheduled and preserving guest frame/time.
`cutscene_script_audio` suppresses `004a7130` only when the current context is
owned by synchronous completion; DirectSound is no longer gated. The existing
dialogue component continues to stop the active handle and suppress the two
exact response/line presenters.

Windowless, timed-silent, unbounded/unpaced live gates pass 11/11 and 10/10.
Both complete once with controls restored and unchanged frame/time, suppress
all reached dialogue presentation, and consume both authored sound commands
silently. Unit tests cover native graph execution and the ordinary/foreign
positive controls for the exact sound handler.
