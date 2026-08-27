---
id: 126
title: Cutscene skip starts every dialogue line simultaneously
status: resolved
symptom: Skipping an in-game cutscene completes it in one step but plays all of its dialogue audio simultaneously
tags: cutscene,conversation,audio,user-report
created: 2026-08-27
updated: 2026-08-27
state_items: S002,S005
---

## User requirement

The cutscene player must complete the full owned cutscene synchronously until
control returns, without advancing world time, and must not play any cutscene
audio while that skip is active.

## Root cause

The synchronous player repeatedly called retail `chooseResponse` without the
normal input path's active-voice cancellation. Its `beginResponse` call at
`00458700` started response audio and returned true specifically to defer
response application until a later presentation frame. The one-step player
immediately revisited the deterministic response instead. In addition,
BehavEd/event work could launch an adjacent conversation whose initial line
used the separate presenter at `0045a170`. Suppressing only the response call
therefore still allowed line audio to start.

`src/native/cutscene_dialogue.c` now owns this cutscene-player policy. It stops
the current retail handle and scopes both exact presenters across the entire
synchronous player invocation. Ordinary playback retains and super-calls both
generated bodies; skip preserves response/script application without starting
either kind of voice. A nested `audio_play_policy` scope also blocks every new
DirectSound buffer start reached from the synchronous player, covering authored
cutscene audio outside the two conversation presenters without muting the
backend or stopping unrelated existing voices.

## Verification

`test_cutscene_dialogue` proves ordinary presenter calls reach both retained
bodies, then proves skip cancellation and suppression without either body. The
windowless, dummy-audio, unbounded/unpaced live cases pass 11/11 and 10/10: the
visible-record case stops one active voice and suppresses five response plus
four line starts; the early case suppresses five plus five. Both block two
additional DirectSound starts, record zero leaked dialogue presentations and a
balanced audio scope, and preserve guest frame/time.
Claim C274 records the production-path evidence and its falsifier.

### Resolution (2026-08-27)
The cutscene player now stops the active dialogue, suppresses RE-grounded response/line presenters, and scopes the DirectSound Play boundary across synchronous completion. Silent unbounded live gates pass 11/11 and 10/10 with two additional audio starts blocked per run, zero dialogue leaks, and unchanged frame/time (C274).
