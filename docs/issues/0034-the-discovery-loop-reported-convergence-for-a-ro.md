---
id: 34
title: The discovery loop reported CONVERGENCE for a round whose run it never let finish
status: resolved
symptom: native_discover: round N found no missing constructor targets -- printed after the run was killed, not after it ended
tags: tooling,discovery,rc-native,diagnostics
created: 2026-08-07
updated: 2026-08-07
---

## Symptom

`tools/native_discover.sh` sat on one round for **fifty minutes**. The run underneath it had reached the game's own busy-wait and was never going to return. When that run was killed by hand, the round parsed no seed addresses and the loop printed

    native_discover: round 10 found no missing constructor targets
      ...
      Whatever stops the run now is not a function static analysis missed.

which is exactly the opposite of what happened: the run had not stopped at all.

## Cause

Two gaps, and each is enough on its own.

1. **The run was unbounded.** The loop invoked `x2native` with no timeout, on the assumption that the run always ends -- true while every run ended in an abort, and false the moment the game got far enough to reach a loop of its own.

2. **The exit status was never looked at.** "No seeds parsed" was treated as convergence, and a killed run parses no seeds. "The run found nothing" and "the run never finished" produced the same output.

This is the project's own rule failing in its own tooling: a negative result has to carry its denominator, and "found nothing" must be distinguishable from "never looked".

## Fix

* `timeout -k 10 ${RUN_TIMEOUT:-300}` around the discovery run.
* Exit codes 124 (timeout), 137 (SIGKILL) and 143 (SIGTERM) are reported as **NOT converged**, non-zero, with the last non-trace lines of the run so the reader can see what it was spinning on. Convergence now also prints the run's own exit status.

## The other half: a hang had no report at all

A crash names a body and an abort names a symbol, but a run that never returns leaves a log that stops mid-sentence. `x2native` now handles SIGTERM/SIGINT and says where it was.

Getting that right took three attempts, and the failures are worth keeping:

* `exit(4)` to get the atexit reports ended in `terminate called without an active exception` -- a C++ teardown in the graphics stack.
* Calling `gpu_draw_report()` from the handler blocked; it touches the device.
* `fprintf` from the handler **deadlocks** whenever the interrupted code holds the stdio lock, which cut the report off halfway through its own first line. A hang diagnostic that hangs.

The handler now writes its message with `write(2)`, which cannot block on a lock, and attempts the ring dump behind an `alarm(5)` -- best effort, and it says so. The guaranteed part is always complete.
