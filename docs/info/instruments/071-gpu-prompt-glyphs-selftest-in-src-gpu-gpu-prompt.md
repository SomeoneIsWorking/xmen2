---
id: I071
kind: instrument
status: trusted
created: 2026-08-26
---

## Instrument

gpu_prompt_glyphs_selftest in src/gpu/gpu_prompt_glyphs.c

## Validated by

The production RGBA atlas upload and draw path renders a full-alpha red cell and a half-alpha red cell over blue in one offscreen frame, reads the pixels back, requires full red > half red by 30 and at least 100 changed pixels, and separately requires untouched transparent pixels to remain blue. The 2026-08-26 windowless vk selftest observed 2,256 changed pixels, full R=255 and half R=128; the unequal controls prove it can report more than uniform success.

## Known failure modes

(none recorded yet)
