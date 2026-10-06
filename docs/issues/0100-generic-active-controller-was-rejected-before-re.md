---
id: 100
title: Generic active controller was rejected before retail prompt naming
status: resolved
symptom: A PlayStation or generic assigned controller remains the active input but its binding row falls back to the keyboard prompt
tags: input,prompts,hotswap,glyphs,playstation
created: 2026-08-22
updated: 2026-08-22
---

## Root cause

The binding-source selector required both active ownership and Xbox-glyph support. Glyph family is presentation policy, not source policy, so a non-Xbox controller could never win row selection.

## Resolution

Any active assigned pad now wins row selection. Xbox pads use the native glyph mapping; PlayStation/generic pads super-call the retail physical-name function. test_pad_glyphs drives both families.
