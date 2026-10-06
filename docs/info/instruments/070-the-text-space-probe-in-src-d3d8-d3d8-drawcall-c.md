---
id: I070
kind: instrument
status: DISTRUSTED
created: 2026-08-26
distrusted_on: 2026-08-26
---

## Instrument

the text-space probe in src/d3d8/d3d8_drawcall.c (X2_TEXT_SPACE), added in fa2ace8 and deleted in f180f16

## Validated by

NOT VALIDATED, AND ITS ONE RESULT IS WITHDRAWN. It reported a clean-looking negative -- 10,815 draws scanned, 366,392 vertices compared, 0 unreadable, no vertex within 57 units of a harvested text corner -- and that negative became the architectural conclusion 'the engine's text coordinates never reach the vertex stream, so the port cannot draw its harvested rectangles'. It could not have produced a positive. Two defects, both readable in 'git show fa2ace8 -- src/d3d8/d3d8_drawcall.c': (1) it capped every draw at 256 vertices, silently and with no counter, and the UI batch is the LARGEST draw in the frame -- frame 350's draw 92 of 94 is one tristrip of 424 primitives on texture 71 -- so it truncated exactly the draw that could have matched; (2) it compared RAW stream vertices against text-space corners with no transform applied, while every draw carries its own mvp (frame 350's UI batch: x 0.00292969, y 0.00520833, an orthographic UI projection), so if the sink leaves coordinates in text space and the mvp does the work, it was blind to that by construction. It was never run against a case that MUST come out positive. Note the shape: three earlier defects in this same probe were found and fixed, each one making the negative look better earned, and the fourth and fifth went unnoticed because a negative that survives repair reads as robust. LESSON: fixing a diagnostic's bugs is not the same as validating it. Validation is feeding it a case that must come out positive; nothing else counts, and a negative from an instrument that has never shown the other answer is worth nothing however many denominators it carries.

## Known failure modes

(none recorded yet)

## DISTRUSTED 2026-08-26

It was never fed a case that must come out positive, and its only result -- the negative that produced the 'text space never reaches D3D8' conclusion -- is withdrawn: a 256-vertex cap truncated the only draw large enough to match, and it applied no transform. Deleted in f180f16; any replacement must drop the cap, apply the draw's own mvp, and prove it fires on a known-present coordinate before its answer is read.

> Every result this instrument produced is suspect until it is re-validated.
