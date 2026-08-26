---
id: C230
kind: claim
status: holds
created: 2026-08-21
tags: input,prompts,keyboard
depends: src/native/prompt_labels.c#prompt_label_rewrite, src/native/prompt_glyph_metrics.c#x2_prompt_glyph_publish_metrics, src/gpu/gpu_prompt_glyphs.c#gpu_prompt_glyphs_render
---

## Claim

Keyboard action labels render the current binding name on a composed keycap without allocating one glyph per key

## Evidence

The original font-builder route established the left/middle/right keycap and invisible -8 advance-rewind metrics from the shared keyboard cap; tests/test_pad_glyphs.c verifies distinct ENTER and one-character A compositions, pad stripping and ??? pass-through. The current native route retains those metrics in memory while owning the pixels in its RGBA atlas. A windowless, silent, unbounded run captured ENTER centered on one native SVG cap with CONTINUE correctly spaced at scratch/screenshots/svg-final-unbounded.png; scratch/logs/svg-final-unbounded.log records 1,188 harvested and submitted quads in 99 semantic batches, with zero desync, queue overflow, transform refusal or GPU refusal.

## What would falsify it

A printable rebound keyboard name returns square brackets, baked key-specific art, missing/overlapping cap geometry, or changes the advance of following prompt text in a real run
