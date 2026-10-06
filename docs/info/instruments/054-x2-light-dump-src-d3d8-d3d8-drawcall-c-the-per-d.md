---
id: I054
kind: instrument
status: trusted
created: 2026-08-14
---

## Instrument

X2_LIGHT_DUMP (src/d3d8/d3d8_drawcall.c) -- the per-draw lighting inputs of a level frame

## Validated by

DISTRUSTED as it stood on 2026-08-14, and fixed: its 'busy frame' threshold was 300 draws on the belief that a level frame submits ~600, but the tutorial's gameplay frames -- the ones photographed with black characters -- submit 138 to 153. Every dump it produced therefore came from the LOADING frames that pass 300, and one such reading was written up as a fact about gameplay (C193, falsified). Now: the default threshold is 100, X2_LIGHT_DUMP_SKIP aims it past the first n qualifying draws, every dump line carries its presented frame number, each kept screenshot prints its own frame number and draw count so the two can be lined up, and the shutdown report ALWAYS prints how many draws qualified, how many were skipped and how many were printed -- so a dump that never fired cannot look like a dump that found nothing.

## Known failure modes

(none recorded yet)
