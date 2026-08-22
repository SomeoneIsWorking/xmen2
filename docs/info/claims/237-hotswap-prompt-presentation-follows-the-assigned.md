---
id: C237
kind: claim
status: holds
created: 2026-08-22
tags: input,prompts,hotswap,tests
depends: src/input/player_input.c#x2_player_input_pad_is_active_source, src/native/pad_glyphs.c#row_pad_binding, tests/test_pad_glyphs.c#main, tests/test_dialog_prompts.c#main
reconfirmed: 2026-08-22
verified_at: 2026-08-22 13:39:08
---

## Claim

Hotswap prompt presentation follows the assigned player's last active input source rather than global controller presence

## Evidence

test_player_input drives one player with both keyboard and a persistent pad, proves initial keyboard mode, gamepad-activity transition, keyboard-activity transition, disconnect fallback, rejection of a different identity, and exact-identity reassociation. test_pad_glyphs proves Xbox and generic/PlayStation pad bindings win only while that assigned pad is active, with native glyphs limited to Xbox and the retail physical-name path retained otherwise. test_dialog_prompts proves the scoped asset/PC text selector in both directions.

## What would falsify it

A player with both sources displays a controller glyph or controller tutorial prose after keyboard activity, displays keyboard presentation after assigned-pad activity, or changes mode because an unassigned controller connects.

## Re-confirmed 2026-08-22

test_player_input drives last-active source transitions and exact stable-identity reassociation. test_pad_glyphs proves Xbox and generic/PlayStation active assigned pads both win source selection, with native glyphs only for Xbox and the retail physical-name super-call otherwise. test_dialog_prompts proves scoped controller/keyboard prose selection.
