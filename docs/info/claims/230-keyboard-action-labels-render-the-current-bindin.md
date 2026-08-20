---
id: C230
kind: claim
status: holds
created: 2026-08-21
tags: input,prompts,keyboard
depends: src/native/prompt_labels.c#prompt_label_rewrite, tools/make_pad_font.py#rasterise_keycap
---

## Claim

Keyboard action labels render the current binding name on a composed keycap without allocating one glyph per key

## Evidence

The shipping font builder published left/middle/right keycap art plus an invisible -8 advance rewind from the shared keyboard cap; tests/test_pad_glyphs.c verifies distinct ENTER and one-character A compositions, pad stripping and ??? pass-through. A real --no-window tutorial run with no pad connected (input probe: 0 pad rows, 42 keyboard rows) captured ENTER centered on one cap with CONTINUE correctly spaced at scratch/screenshots/keycap-windowless.png; the builder reported 19 drawing glyphs, 5,580 changed pixels, all confined to published cells.

## What would falsify it

A printable rebound keyboard name returns square brackets, baked key-specific art, missing/overlapping cap geometry, or changes the advance of following prompt text in a real run
