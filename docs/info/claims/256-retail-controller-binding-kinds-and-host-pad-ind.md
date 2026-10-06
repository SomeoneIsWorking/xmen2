---
id: C256
kind: claim
status: holds
created: 2026-08-24
tags: input,prompts,hotswap
depends: src/native/dinput8_controller_slots.c#dinput8_controller_slot_for_host_pad, src/input/player_input.c#x2_player_input_sync, src/native/pad_glyphs.c#host_pad_for_kind, tests/test_dinput8_controller_slots.c#main
---

## Claim

Retail controller binding kinds and host pad indices are translated by attached instance GUID, so reorder cannot select the wrong prompt family

## Evidence

test_dinput8_controller_slots proves both directions across reordered, detached and unknown slots. test_player_input republishes unchanged assignments under reordered guest kinds and suppresses unresolved bindings. test_pad_glyphs proves reordered Xbox/generic family and active-source routing.

## What would falsify it

A detached/unresolved guest slot publishes a gamepad binding or prompt, or a slot reorder leaves glyph family/activity bound to the old host index.
