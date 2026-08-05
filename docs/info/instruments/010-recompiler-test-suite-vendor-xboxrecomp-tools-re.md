---
id: I010
kind: instrument
status: trusted
created: 2026-08-05
---

## Instrument

Recompiler test suite (vendor/xboxrecomp/tools/recomp/test_lifter.py + test_regressions.py)

## Validated by

Run against BOTH classes on real data, not reasoned about. With the CFG flag-propagation fix reverted: 2 failures naming sub_0026E740's always-false branch. With it in place: 15 pass in 1.1s. It also caught its own weakness -- the first synthetic test of that bug PASSED on the broken code, because the shape that breaks is not the shape that is obvious to write, which is why the regression cases translate real functions out of the shipped binary. When the binary is absent they SKIP with the path in the message rather than passing quietly.

## Known failure modes

(none recorded yet)
