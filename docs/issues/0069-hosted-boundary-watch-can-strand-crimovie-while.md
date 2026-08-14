---
id: 69
title: Hosted boundary watch can strand CriMovie while the normal build progresses
status: resolved
symptom: WATCH=1 x2run turns permanently black after the intro; boundary count freezes in libMovie::appendInfo while the normal WATCH=0 build keeps rendering
tags: pc,hosted,x86watch,movie,timing,instrumentation
created: 2026-08-14
updated: 2026-08-14
---

## Cause

The game was not regressed. Every sustained-black run used the `WATCH=1` hosted build. The boundary watcher changes timing enough that CriMovie can strand its rearmed one-shot multimedia timer: `igCriMovieCodec::loadMovie` waits on event `DAT_1014a218`, the timer callback counter `DAT_10057300` remains 1, and all three movie workers stay parked. A normal `WATCH=0` build at the same DLL layout progressed through the movie phase and rendered richly nonblack late samples.

## Discriminators

* Failing watched run: boundary crossings stopped at 14,979,992 inside mapped `libMovie!igMovieManagerNoInsight::appendInfo`; CriMovie callback count stayed 1 across repeated reads.
* Normal preferred-base run: late samples resumed nonblack rendering.
* Normal 130-second verification: samples 7-13 all nonblack, 3,442-5,202 distinct colors, process alive.
* Relocating CriMovie also made the watched run progress, but a preferred-base normal run progressed too. Address layout changes timing; it is not the cause.
* A minimal Wine harness rearming the same `TIME_ONESHOT` timer produced 1,667 callbacks at image base 0x02000000 and 1,655 at 0x10000000. A high callback address is not the cause.

## Resolution

Do not use a `WATCH=1` build as a progression or liveness oracle for the intro movie path. Use it only for bounded entry/exit evidence, and reproduce behavioral failures in the normal build before treating them as game defects. The attempted DLL-layout guard was removed as a timing bandaid.
