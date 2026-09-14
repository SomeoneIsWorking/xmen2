---
id: 150
title: Longer blocks stretch the fixed-crossing preemption quantum
status: investigating
symptom: a preemption slice covers ~1.8x more guest work after blocks lengthened; libCriMovie's decoder spin hand-off may wait longer
tags: jit,wasm,preemption,threads
created: 2026-09-14
updated: 2026-09-14
---

The wasm block-continues-past-a-conditional change (x86port 75b2cec) raised a real title's instructions per translated block from 5.4 to 9.7. Preemption is bounded by guest_quantum() in src/native/threads.c, a fixed 20000 BOUNDARY CROSSINGS (g_quantum, settable through the registered 'quantum' CVar). A crossing is a host-import boundary, so with longer blocks the same 20000 crossings cover ~1.8x more guest instructions, and a preemption slice is correspondingly longer.

Why it could matter: src/native/threads.c documents that libCriMovie's decoder rendezvous is a SPIN on both sides -- the decoder runs until dry then parks itself with SuspendThread, while its partner spins up to 3,000,000 times calling ResumeThread until the decoder clears its flag -- and that what schedules two spinners is exactly this preemption ('two hand-off designs were measured and both made it worse').

Unproven either way. Nothing measured shows it (the 300 s driven route improved 1063.1 -> 666.6/681.7 ms average frame with 5.4 -> 9.7 instructions per block) and nothing rules it out: that route does not reach a movie. A first probe at quantum=4000 was inconclusive (8 boot-phase intervals) so NO conclusion about the quantum exists; an earlier reading that a smaller quantum was worse is retracted.

Falsifier and next step: bound the slice by WORK rather than by crossings (guest instructions retired, or wall time), which is stable across block formation, then measure a route that plays a movie -- preemption latency from the heartbeat's 'preemption(s) at a quantum of N crossing(s)', the movie chapter's wall time, and audio/video continuity. If a work- or time-bounded slice changes none of those, this is resolved as a non-finding.
