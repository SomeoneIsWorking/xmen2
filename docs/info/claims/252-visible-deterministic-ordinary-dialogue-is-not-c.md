---
id: C252
kind: claim
status: holds
created: 2026-08-22
tags: cutscene,conversation,regression
depends: src/native/cutscene_player.c#active_sequence
reconfirmed: 2026-08-27
verified_at: 2026-08-27
---

## Claim

Visible deterministic ordinary dialogue is not a cutscene and ignores the
cutscene-player action-20 path unless an authored control-lock epoch exists.

## Evidence

The production owner begins only when retail `lockControls` writes a negative
duration while a BehavEd context is current. Conversation visibility never
creates a sequence. The pure production policy's inactive case returns without
an invocation, and the wiring audit proves conversation.c still consumes only
retail Accept action 4; it has no action-20 or cutscene-player dispatch.

## What would falsify it

Visibility alone creates a cutscene-player sequence, or Escape/Start advances
an unlocked ordinary deterministic dialogue through the cutscene path.
