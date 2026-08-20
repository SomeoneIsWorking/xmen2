---
id: C218
kind: claim
status: falsified
created: 2026-08-19
tags: boot,testing,gameplay
depends: src/native/startup.c, src/native/input_probe.c
falsified_on: 2026-08-20
---

## Claim

X2_BOOT_MAP produces a run with NO player character: all five hero handles (0x0070b814[0..3] and the 0x0072988c fallback) stay 0, where a normally-booted run resolves player 0's. Anything downstream of the player actor therefore behaves differently in a boot-map run, and such a run is not comparable to a played game.

## Evidence

Same build, same probe (tools/x2ctl.py input), two boot paths: X2_BOOT_MAP gives '0 of 5 hero handle(s) resolve to an actor'; a normal boot driven through the menus gives 'player handle 0x00000a01 -> actor 0x082e7010'. The consequence was measured end to end as issue #83: with no hero the conversation start cannot resolve a speaker, takes the fallback that bases the seen-line bitmap at 0, and the tutorial's second conversation is suppressed (flags 0x18->0x10, no line) so the level's unlock script never runs. On the normal path the same conversation starts (flags 0x18->0x13, line 0x40), conv_0020b_end launches and the run reaches free-roaming gameplay with a full HUD.

## What would falsify it

if a boot-map run is ever observed with a non-zero hero handle -- which would mean the preamble it skips is no longer where the party is built

## FALSIFIED 2026-08-20

The observation was true of the old bare-load implementation, but src/native/startup.c now calls retail startFirstMission before loading. A live repaired run measured player 0 handle 0x00000201 resolving to actor 0x08326010 and tutorial conversation 0020b entering speaking/visible state (flags 0x18 -> 0x13).

> Anything that cited this claim as proof must be re-checked. Grep the repo for it.
