---
id: 120
title: Native SVG keycap sat below its stock label as an underline
status: resolved
symptom: The ENTER keycap background rendered below the word, with only a low bar and right bracket visible around the prompt.
tags: pc,native,graphics,svg,prompts,text,fonts,baseline,libIGGfx
created: 2026-08-26
updated: 2026-08-26
state_items: S004
---

## Cause

`prompt_glyph_metrics.c` published the port cells' width, height, advance and
horizontal offset but left the engine glyph-record baseline at `+0x08` as zero.
The retail letters use the font baseline (42 after the configured 2.64 text
scale), so the SVG rectangle was laid out one ascent below the stock ASCII even
though its engine transform and batch timing were correct.

## Fix and evidence

The runtime now derives the modal non-zero baseline from the same font record
after `ui_text_scale` has scaled it, and copies that font-owned value into each
otherwise-unused port cell. No screen offset is applied.
`tests/test_prompt_glyph_metrics.c` proves the mode is selected rather than the
first value and is not double-scaled. `scratch/screenshots/svg-baseline.png`
shows ENTER enclosed; `scratch/logs/svg-baseline-160.log` records 1,344 quads
across 112 batches with zero desync, transform refusal, or GPU refusal under
windowless, silent, unbounded execution.

## Ruled out

The prior low placement was not a stale D3D transform: moving submission to the
nested `updateContextState` boundary made the correct engine matrix current but
retained the low bar until the baseline metric was published.
