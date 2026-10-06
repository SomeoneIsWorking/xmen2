---
id: C176
kind: claim
status: holds
created: 2026-08-14
tags: audio,dsound,native,engine
---

## Claim

The native game drives real PCM through the host audio device: XMen2.exe's DirectSound boundary creates, fills, duplicates and plays secondary buffers which the SDL3 mixer submits as nonzero stereo samples.

## Evidence

Static RE of XMen2.exe FUN_00594290 and audio functions FUN_00594590/FUN_00594e50/FUN_00596050 established the DirectSound COM surface. In scratch/logs/dsound-gameplay-final.log, a real route to frame 2900 creates 270 secondaries and 18 duplicates, performs 485 Lock and 28 Play calls, and the SDL callback submits 2,665,472 frames containing 1,249,706 nonzero samples; the run exits cleanly at its frame limit. dsound_selftest separately distinguishes shared PCM/independent duplicate cursors, mute, one-shot stopping, and growth past the old 256-object failure.

## What would falsify it

A real run that produces zero nonzero mixed samples after secondary Play calls, reaches an unwritten DirectSound method, fails buffer allocation at a reachable corpus size, or emits PCM with cursor/format/control behavior that differs from the original DirectSound path.
