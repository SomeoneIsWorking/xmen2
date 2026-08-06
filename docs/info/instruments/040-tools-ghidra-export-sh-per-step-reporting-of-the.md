---
id: I040
kind: instrument
status: trusted
created: 2026-08-06
---

## Instrument

tools/ghidra_export.sh -- per-step reporting of the lift pipeline

## Validated by

CAUGHT LYING and now guarded. Every step (seed/split/merge/recreate/table-seed) appended to one log and then grepped the WHOLE file, so a step that DIED still printed the previous run's lines. Measured: a Jython SyntaxError (one non-ASCII character in a comment) made RecreateFunction.py fail to compile; the script announced 'recreate 42 function body/bodies', printed six RECREATE lines from the run before it, and the pipeline exported, re-emitted and rebuilt as though 42 bodies had been repaired. analyzeHeadless exits 0 when a script fails to compile, so the '|| exit 1' guard never fired. Now every step goes through run_step(), which records the log length first, looks ONLY at lines this run added, refuses on a Jython SyntaxError/Traceback, and refuses when the step produced no output of its own. Validated against BOTH classes by 'tools/ghidra_export.sh --selftest', which feeds it a reporting step, a silent step, a script that would not compile, and a silent step preceded by an EARLIER run's output -- the last is the exact case that fooled it. Wired in as the 'lift_step_guard' ctest.

## Known failure modes

(none recorded yet)
