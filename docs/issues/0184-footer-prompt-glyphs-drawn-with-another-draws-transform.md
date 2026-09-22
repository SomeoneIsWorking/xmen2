---
id: 184
title: footer prompt glyphs were drawn with another draw's transform, and some had no glyph at all
status: resolved
symptom: with a controller, pause and team footers read "Back / Scroll / Rotate" with no button art; stick prompts showed stock text; keyboard keycaps vanished after the stick icons were added
state_items: S007
tags: input,pad,prompts,glyphs,text
created: 2026-09-22
updated: 2026-09-22
---

# 0184 — footer prompt glyphs were drawn with another draw's transform

State item: S007 (source-sensitive prompts)

## Symptom

Reported by the user with a controller: the footer "Details / Accept" prompts
had no glyphs. Reproduced on a pad-owned Player 1 (1280x720): the pause footer
drew "Back", "Scroll" and "Rotate" labels with nothing beside them, while the
conversation's `A` beside `CONTINUE...` rendered.

## Causes

Three independent ones.

1. **Attribution.** Every prompt quad harvested in a frame was drawn at the
   first finalized non-indexed draw, with that draw's transform. A text pass
   lays all its strings out before drawing any, and footer labels are drawn by
   later draws with a different transform, so their art was placed where the
   first draw's text plane put it — off the footer. Measured: one draw submits a
   contiguous window of laid-out strings (conversation: speaker 7 + line 59 +
   `A` 1 = one 67-glyph draw), and draw order need not follow layout order.
   Fix: `prompt_glyph_quads.c` keeps a layout record of every string (non-
   prompt strings as empty runs); the finalizer takes the earliest contiguous
   window whose glyph count equals the draw's `6 * glyphs - 2` primitives and
   renders those quads with its own transform. Live: 956/956 prompt windows
   taken, 0 never drawn.
2. **Stick directions had no icon.** `pad_glyph_code` did not map the axis
   codes (left stick 1..4, right stick 7..10), so "Scroll"/"Rotate" kept the
   stock name. Eight direction icons were added to `glyphs.json`.
3. **Retail bytes.** Codepoints were assigned sequentially from `0x80`. Byte
   `0x8C` (Œ) was always drawn by the retail fonts, so d-pad up was silently
   unavailable; growing the run to 24 icons pushed the keycaps onto `0x99` (™)
   and `0x9C` (œ), which disabled every keyboard keycap. Measured over all four
   font records the game offers, those three are the only drawing bytes in
   `0x80..0x9F`; `0xA0` up is dense Latin-1. Fix: `retail_font_codepoints` in
   the manifest; assignment skips them, the atlas emits an unpublished cell for
   each, and the manifest refuses a run reaching `0xA0`.

## Evidence

`scratch/prompts/pad.py`-style live runs (pad-owned P1 and keyboard P1, tutorial
map): pad footer shows B Back, left-stick Scroll, right-stick Rotate; the
conversation shows the `A` glyph; the keyboard pause footer and conversation
show the `Esc`/`Enter` keycaps. The metrics log reports no collision:
"published 28 cell(s)". `test_prompt_glyph_batch` exercises the window matcher
against the real store (a matcher ignoring the glyph count fails 5 checks);
`test_prompt_glyph_metrics` covers the skipped retail bytes.
