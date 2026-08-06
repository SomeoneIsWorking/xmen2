---
id: 28
title: A Ghidra lift step that died still printed the PREVIOUS run's output, and the pipeline built on it
status: resolved
symptom: ghidra_export.sh says '== recreate 42 function bodies ==' and prints RECREATE lines, but the database is unchanged; the same lines appear every run
tags: pc,recomp,rc-lift,ghidra,tooling,instrument
created: 2026-08-06
updated: 2026-08-06
---

## What happened

`tools/ghidra_export.sh --recreate <42 addresses>` announced 42 bodies, printed six `RECREATE:` lines, and went on to export, re-emit and rebuild. None of the 42 were touched. The six lines belonged to the PREVIOUS run.

## Root cause

Two independent faults, either alone enough:

1. **The script never ran.** One non-ASCII character (a `…` in a comment I had just added) made Jython refuse to compile `RecreateFunction.py`: `SyntaxError: Non-ASCII character ... but no encoding declared`. Jython 2.7 is Python 2 and needs a `coding:` line for anything outside ASCII.
2. **Nothing noticed.** `analyzeHeadless` exits **0** when a post-script fails to compile, so the `|| { echo failed; exit 1; }` guard never fired. And every step appends to ONE log and then greps the WHOLE file (`grep -E '^RECREATE:' $LOG | tail -6`), so the previous run's lines were printed as this run's result.

The second is the real defect. The first is a typo; the second turns any failure of any step -- seed, split, merge, recreate, table-seed -- into a silent pass with convincing output.

## Fix

`run_step()` in `tools/ghidra_export.sh`, which every step now goes through:

- records `wc -l` of the log BEFORE the step and looks only at lines this run added;
- refuses on `SyntaxError`/`Traceback`/`*Error:` in those lines, saying the script did not RUN;
- refuses when the step produced no line matching its own prefix, saying the output above belongs to an earlier run.

Proven to fire by `tools/ghidra_export.sh --selftest` (needs neither Ghidra nor the install), which feeds it four cases including the exact one that fooled it: a silent step preceded by an earlier run's output. Wired in as the `lift_step_guard` ctest.

## What it cost

The rebuild and run after it looked like a legitimate result and was reasoned about as one. Any conclusion drawn from a lift between the two runs would have been about an unchanged database.

## Related

Same class as issue #12 (a program reused from the project without checking which file it came from) and the reason `RecreateFunction.py` has a postcondition at all.
