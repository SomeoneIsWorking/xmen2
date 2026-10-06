---
id: I055
kind: instrument
status: trusted
created: 2026-08-15
---

## Instrument

X2_LIGHT_SURVEY (src/d3d8/d3d8_drawcall.c)

## Validated by

Shown able to produce BOTH answers: it reported 235 bounded-black draws on an interactive run and 1 of 107800 on a scripted one, and it was caught lying once -- gated per-draw on X2_LIGHT_DUMP_MIN it surveyed only the last ~20 draws of each 140-draw frame while printing 421 as the denominator. The gate now opens once at the first gameplay frame and counts every draw after it; its 'unlit' counter was zero by construction until fill_lighting was changed to call it for unlit draws too.

## Known failure modes

(none recorded yet)
