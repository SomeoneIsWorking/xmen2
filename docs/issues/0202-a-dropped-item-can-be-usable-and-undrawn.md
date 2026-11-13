---
id: 202
title: a dropped item can be usable and undrawn
status: open
symptom: "on the phone, after killing enemies, an item's prompt and pickup worked with no model drawn; not under any character, not every time"
state_items:
tags: gameplay,render,phone
created: 2026-11-03
updated: 2026-11-03
---

# 0202 — a dropped item can be usable and undrawn

## Observed

Reported from the HONOR phone: "There is an invisible item here", "Enemy drop
probably", "the invisible item doesn't happen always and I made sure it
wasn't under anyone". Not reproduced on desktop.

## What retail allows

`setItem` `FUN_00479cc0` ignores `setModel`'s answer, so a pickup whose model
was refused keeps its label, touch and use activation and has no model
(`external/xmen2-decomp/docs/notes/pickups.md`). setModel refuses when the instance or a component pool
is full, when a new file meets a full model file cache or resource name table,
and when the file does not load. Retail logs none of these.

## What the port now reports

Each refusal logs one `model` error line as the pickup drops, naming the
pickup, its item label, the model path and the reason. `GET /pickups` lists
every pickup with `model.loaded` and the pool counts, and `without_model`
counts the undrawn ones. `tools/live_case.py pickup-models` shows both on
desktop by making the money models unloadable.

## Next

When it happens again: `GET /pickups` over the phone's control channel while
the item is on screen, and the run log's `setItem 0x00479cc0` line. A
`loaded: false` pickup with no log line means something released its model
after it dropped; `loaded: true` means the model is attached and the draw
path lost it.
