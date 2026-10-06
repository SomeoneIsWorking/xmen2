---
id: 192
title: Advanced Options text aborted the prompt glyph interceptor
status: resolved
symptom: Escape in Options opened the PC Advanced Options screen (sebas) and the game aborted with "text writer at ECX has no readable vertex cursor"
state_items: S007
tags: prompts,glyphs,text,menus,crash
created: 2026-10-06
updated: 2026-10-06
---

# 0192 — Advanced Options text aborted the prompt glyph interceptor

## Reproduction

`tools/live_case.py options-back`: main menu, Options, Escape. The game opens
`sebas` (Advanced Options) and drew its "#ESC# Back" footer through the glyph
loop `FUN_005ee780`.

## Cause

`FUN_005ee400` appends each glyph through the writer's sink, `[writer]`
vtable `+0xc`. `prompt_glyph_draw.cpp` modelled only the text batch
(vtable `0x0069c904`, `+0xc` = `FUN_005840a0`), whose vertex cursor it keys
quads by. Advanced Options writes through vtable `0x006a4c4c`, whose `+0xc` is
`FUN_005f64f0`: a screen-space list of 0x1c-byte vertices counted at
`sink+0x6d64` (capped at 1000, y flipped by `DAT_00a0a000`). No
`drawNonIndexed` finalizer places it, so the key read failed and aborted.

The emitter's writer is the glyph loop's second stack argument (`[ESP+0x64]`
at `0x005eeae3`, which is entry `ESP+8`), the same object whose `+8` is the
colour the override already pre-reads.

## Fix

`x2_override_005ee780` arms interception only when the writer's sink has the
text batch's vtable; any other sink keeps the whole string stock and is counted
("because the writer was not the text batch"). Advanced Options therefore
shows the stock "Esc Back" text.

## Open

A second Escape does not leave Advanced Options in the headless case; whether
that screen takes keyboard Back at all on retail PC is not established.
