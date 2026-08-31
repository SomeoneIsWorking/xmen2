---
id: 139
title: Touch layout conflicts with retail HUD and lacks direct hero selection
status: investigating
symptom: Android touch overlay crowds gameplay with a second camera stick and controller glyph grid; retail HP/energy, potion, and portrait HUD stays in bottom-left and portraits cannot be tapped to select heroes
tags: android,touch,hud,input,ui
state_items: S018
created: 2026-08-31
updated: 2026-08-31
---

## Root cause

The first Android layout exposed controller-shaped controls directly from the
retail persistence vocabulary. Names such as `TargetLock` do not describe the
actual Xbox/PS2 binding (health pack), so a mechanically labeled overlay lied
about working actions. It also made camera movement a second visible stick and
left the retail CHud at its desktop bottom-left anchor. Touch contacts were
consumed before the ordinary SDL-to-Win32 mouse path, so portrait touches could
not reach the retail click handler that already performs direct hero selection.

## What was tried / dead ends

Cycling heroes by publishing new next/previous actions would duplicate the
retail portrait handler and would incorrectly imply that the working physical
D-pad bindings should be removed. The D-pad remains mapped exactly as retail;
only touch presentation uses the proven player-facing action names.

## Resolution

Implemented pending installed-APK visual verification. The touch action owner
now provides one left stick, labeled bottom-right combat/ability buttons, and
an invisible relative camera-swipe region backed by Lucent's capture origin.
Top-right portrait contacts publish ordered Win32 mouse movement and left-button
messages through the existing retail path. `FUN_005a43d0` remains the one CHud
draw path: a scoped native override mirrors its authored root to the top-right,
while the retained `FUN_005a3320` character-panel body is remapped to the
top-left. The original vectors are restored after each super-call, and desktop
behavior is unchanged because relocation requires the Android touch overlay.
