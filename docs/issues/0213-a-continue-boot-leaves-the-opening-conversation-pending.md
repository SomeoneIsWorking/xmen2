---
id: 213
title: a Continue boot leaves the opening conversation pending
status: open
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

## Expected

`external/xmen2-decomp/docs/notes/cutscene_player.md` (jungle1 from the
shipped autosave, 2026-10-07): `0x10` while the level's opening conversation
is queued, `0x13` while it is shown, `0x18` while it ends.

## Next

Find what moves a queued conversation to shown after a map load and why it
does not run after the Continue boot's direct save-chain dispatch
(`boot_mode_runtime`, `startup.cpp`).
