---
id: 150
title: Longer blocks stretch the fixed-crossing preemption quantum
status: resolved
symptom: a preemption slice covers ~1.8x more guest work after blocks lengthened; libCriMovie's decoder spin hand-off may wait longer
tags: jit,wasm,preemption,threads
created: 2026-09-14
updated: 2026-09-24
---

The wasm block-continues-past-a-conditional change (x86port 75b2cec) raised a real title's instructions per translated block from 5.4 to 9.7. Preemption is bounded by guest_quantum() in src/native/threads.c, a fixed 20000 BOUNDARY CROSSINGS (g_quantum, settable through the registered 'quantum' CVar). A crossing is a host-import boundary, so with longer blocks the same 20000 crossings cover ~1.8x more guest instructions, and a preemption slice is correspondingly longer.

Why it could matter: src/native/threads.c documents that libCriMovie's decoder rendezvous is a SPIN on both sides -- the decoder runs until dry then parks itself with SuspendThread, while its partner spins up to 3,000,000 times calling ResumeThread until the decoder clears its flag -- and that what schedules two spinners is exactly this preemption ('two hand-off designs were measured and both made it worse').

Unproven either way. Nothing measured shows it (the 300 s driven route improved 1063.1 -> 666.6/681.7 ms average frame with 5.4 -> 9.7 instructions per block) and nothing rules it out: that route does not reach a movie. A first probe at quantum=4000 was inconclusive (8 boot-phase intervals) so NO conclusion about the quantum exists; an earlier reading that a smaller quantum was worse is retracted.

Falsifier and next step: bound the slice by WORK rather than by crossings (guest instructions retired, or wall time), which is stable across block formation, then measure a route that plays a movie -- preemption latency from the heartbeat's 'preemption(s) at a quantum of N crossing(s)', the movie chapter's wall time, and audio/video continuity. If a work- or time-bounded slice changes none of those, this is resolved as a non-finding.

### Note (2026-09-14)
MEASURED FALSIFICATION ATTEMPT: the quantum was run at two values on the same route with the same page command line -- 4000 and 20000 -- and the two were indistinguishable (6700.3 ms and 6406.1 ms over 8 intervals). So the quantum value does not explain those runs, and this issue has no positive evidence behind it. The untested part remains the one that matters: a route that actually plays a movie, where the libCriMovie spin hand-off would show. Until that is measured, treat the longer-block/quantum interaction as unmeasured rather than either confirmed or excluded.

### Resolution (2026-09-24): a non-finding, measured on a route that plays movies

The premise misread what the quantum counts, and the shipping path does not
use the code it worried about.

- **The quantum is not a crossing count.** `guest_quantum()` runs after every
  `x86p_jit_engine_run` return (`src/native/x86_engine.c`), and most runs end
  at a host crossing. Whenever another thread waits, the turn is therefore
  offered at every crossing. The quantum is the run's step budget, in JIT
  steps (block entries), capped at 200,000. It bounds only guest code that
  crosses nothing. So longer blocks lengthen only a crossing-free spin; the
  hand-off at crossings is unchanged. The heartbeat, the thread report, the
  cvar's log line and the codemap all said "crossings". They now say what is
  counted.
- **Movies do not run libCriMovie's spin rendezvous by default.** The retail
  boot logs `movie: native FFmpeg SFD playback`; the guest CriMovie bodies run
  only under `X2_NATIVE_FMV=0`, a diagnostic.
- **Measured.** Native headless retail boot, 60 s through the intro movies,
  once at the default quantum of 20,000 and once at 2,000. Preemptions did not
  rise with the 10x smaller quantum (per 5 s: 3,105 / 11,037 / 9,020 against
  3,397 / 6,264 / 6,494). About 2,200 hand-offs per second means an average
  turn of about 0.45 ms against a 33 ms movie frame. The audio-driven movie
  clock advanced 4.74–4.79 s per 5 s at 20,000 and 4.75–4.92 s at 2,000; that
  is the dummy audio driver's pacing, the same in both runs. Each movie
  chapter changed at the same heartbeat in both runs.
