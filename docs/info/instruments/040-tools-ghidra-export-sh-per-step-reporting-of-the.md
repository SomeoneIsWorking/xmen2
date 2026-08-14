---
id: I040
kind: instrument
status: trusted
created: 2026-08-06
distrusted_on: 2026-08-11
revalidated_on: 2026-08-14
---

## Instrument

tools/ghidra_export.sh -- per-step reporting of the lift pipeline

## Validated by

CAUGHT LYING and now guarded. Every step (seed/split/merge/recreate/table-seed) appended to one log and then grepped the WHOLE file, so a step that DIED still printed the previous run's lines. Measured: a Jython SyntaxError (one non-ASCII character in a comment) made RecreateFunction.py fail to compile; the script announced 'recreate 42 function body/bodies', printed six RECREATE lines from the run before it, and the pipeline exported, re-emitted and rebuilt as though 42 bodies had been repaired. analyzeHeadless exits 0 when a script fails to compile, so the '|| exit 1' guard never fired. Now every step goes through run_step(), which records the log length first, looks ONLY at lines this run added, refuses on a Jython SyntaxError/Traceback, and refuses when the step produced no output of its own. Validated against BOTH classes by 'tools/ghidra_export.sh --selftest', which feeds it a reporting step, a silent step, a script that would not compile, and a silent step preceded by an EARLIER run's output -- the last is the exact case that fooled it. Wired in as the 'lift_step_guard' ctest.

The same selftest now drives the SHIPPING source-provenance predicate too: a
matching recorded hash is reusable; a mismatched hash and an existing program
with no stamp both force re-import; a genuinely new unstamped program stays on
the normal import path. Current libIGSg's stamp exactly equals the installed
DLL's SHA-256, and the independent export verifier reports five agreeing
sections, 4,714 functions and zero truncated bodies (C179, issue #12).

## Known failure modes

* **VOLUME, not content** (found and fixed 2026-08-11, see below). `grep -q` in
  a pipeline under `set -o pipefail` reports failure for large input, so the
  "this step produced NOTHING" refusal fired on the step that did the MOST
  work. Blast radius: only steps that ABORTED with that message can have been
  wrong -- the guard's other direction (accepting an earlier run's output) is
  unaffected, because that path never depended on the pipeline's status. One
  such abort was observed, the msdia80 seeding pass; it was re-run and
  completed. Every other lift step in the tree's history produced output small
  enough that grep read it all before exiting.

## DISTRUSTED 2026-08-11

Its step guard -- 'this step produced NO output of its own, refusing' -- fired on a step that had just created 1267 functions. The guard is `printf '%s\n' "$_added" | grep -qE "$_prefix"`, and grep -q exits at the first match and closes the pipe; under `set -o pipefail` the writer's SIGPIPE (141) becomes the pipeline's status, so the test for 'no output' SUCCEEDS whenever the output is large enough to still be flowing. It reads as a refusal about content and is a refusal about VOLUME, and it fires only on the steps that did the most work -- the seeding pass that made this visible was the biggest one ever run. Measured directly: 700 KB through the same pipeline gives status 141; the same data via a variable matches. FIXED in the same session (match once into a variable, never `| grep -q`), and the selftest now carries a 20k-line case that the old code fails and the new one passes.

> Every result this instrument produced is suspect until it is re-validated.

## REVALIDATED 2026-08-11

Fixed by matching ONCE into a variable and testing that (`_hits=$(... | grep -E
... || true)`), so no reader is killed mid-write and no pipeline status stands
in for a search result. Both classes were run before believing it: 700 KB
through `| grep -q` exits 141, the same data through the new form matches. The
selftest carries the case (`a step that reports a LOT of work`, 20k lines --
~700 KB, an order of magnitude past the 64 KB pipe buffer), it FAILS against
the old code and PASSES against the new, and it runs as the `lift_step_guard`
ctest. The case generates its output in the child rather than passing it as an
argument, because 700 KB in one argv element exceeds this shell's limit and the
case would otherwise fail for a reason unrelated to the guard.
