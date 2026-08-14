---
id: I019
kind: instrument
status: trusted
created: 2026-08-05
---

## Instrument

src/x86watch.c -- entry/exit watch on recompiled PC entry points (X2_WATCH=0x…, X2_WATCH_MEM=0x…, X2_WATCH_LOG)

## Validated by

X2_WATCH_SELFTEST=1 checks BOTH directions inside the shipping DLL before any game code runs: a watched entry point must count a call, and an address that is never entered (0xDEAD0000) must stay at zero. Only the positive half would pass with a watch hard-wired to print on every entry. VALIDATED THE HARD WAY: the first version wrote to stderr and produced a totally silent log on a run where it had definitely fired -- the game is a GUI-subsystem process under 'wine explorer /desktop=' and has no stderr. The silence was indistinguishable from 'the function was never called'. It was caught by running it against the KNOWN-GOOD build (where the function definitely runs), not the failing one; on the failing build the silence would have read as a finding. Sink is now a file. Proven useful immediately: it refuted two conclusions I had drawn from reading the generated C.

## Known failure modes

The watch is trusted for its narrow question -- whether a recompiled boundary
was entered and returned -- but **not as a progression, liveness, or timing
oracle**. A `WATCH=1` x2run build stranded CriMovie after its first rearmed
one-shot multimedia timer callback while the normal build at the same DLL
layout progressed through the movies. Relocating CriMovie also changed the
outcome, but a preferred-base normal build progressed too, proving layout was
only a timing perturbation. Reproduce any behavioral failure without the watch
before attributing it to the game (issue #69).

`X2_WATCH=all` has a separate volume failure: its 32-bit global crossing count
can wrap during a long busy run and re-enable supposedly capped output. It
produced a 593 MB log. Use a specific address and a bounded run; never use
`all` for progression.
