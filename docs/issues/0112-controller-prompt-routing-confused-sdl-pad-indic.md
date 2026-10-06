---
id: 112
title: Controller prompt routing confused SDL pad indices with retail controller slots
status: resolved
symptom: After controller reorder or hotswap, bindings or button prompts can name the wrong device family or stay attached to the old pad index
tags: pc,native,input,controller,prompts,hotswap
created: 2026-08-24
updated: 2026-08-24
---

## Cause

Retail binding kinds 3--12 index the game manager's ten-slot table, while SDL host pad indices index a separate mutable inventory. Treating them as the same number bypassed the game's own admission/reorder state.

## Resolution

`dinput8_controller_slots` translates both directions by the immutable instance GUID in attached retail slots. Publication and glyph/source routing consume that authority; detached, unknown and unresolved slots publish no gamepad presentation. Focused tests cover reorder, detachment, unknown GUID, family selection and last-active source.
