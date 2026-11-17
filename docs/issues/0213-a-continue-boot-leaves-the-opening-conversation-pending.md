---
id: 213
title: a Continue boot leaves the opening conversation pending
status: resolved
symptom: "pickup-models fails 4/8: after a Continue boot GET /party keeps reporting conversation=true and Return never clears it"
state_items:
tags: boot,conversation,live-case
created: 2026-11-17
updated: 2026-11-17
---

# 0213 — a Continue boot leaves the opening conversation pending

## Observed

`tools/live_case.py pickup-models` (`boot.mode=continue`, the seeded
`autosave0.save`) on 2026-11-17: the conversation flags go `0x00 -> 0x10` at
input poll 43, before the boot's map load returns, and stay `0x10` for the
whole run. No line is ever shown (`0x13` never appears), so `GET /party`
reports `conversation: true` throughout and `keyboard_into_gameplay`'s 60 s of
Return never clears it. The case fails "no conversation holds the party" and
"Continue reached gameplay" after the boot and again after
`loadmap act1/deadzone/deadzone2`; its drop checks still pass (2 drops, both
with loaded models), and the frame shows ordinary gameplay.

The same 4/8 result comes from `origin/main` 17398d1 built against x86port
072c994 (the tree of the older pin cb10114), so it predates the plan 212
changes and the x86port pin bump.

`extraction-free` from the same save shows the same `0x10` for 64 s after the
boot; its `loadmap` then shows that map's conversation (`0x17`, `0x1c`,
`0x13`, `0x18`).

## Cause

Nothing was pending. `src/native/conversation_panel.hpp:holds` counted flag
0x10 without 0x8 as a queued conversation, but 0x10 only says the manager is
loaded for the map: its load (vtable `+0x4`, `FUN_00455af0`) sets it as its
last step and its unload (`+0x8`, `FUN_00455d90`) clears it, on every map. The
save's map opens no conversation, so `GET /party` reported one for the whole
run (`external/xmen2-decomp/docs/notes/cutscene_player.md`).

## Fix

`holds` is the shown bit (0x2) alone, which `FUN_004556b0` sets and clears.
`pickup-models` passes 8/8 (10 drops, all with models, drawn in
`shots/drops.png`).
