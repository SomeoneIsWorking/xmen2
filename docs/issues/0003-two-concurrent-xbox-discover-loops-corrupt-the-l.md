---
id: 3
title: Two concurrent xbox_discover loops corrupt the lift and fake a translator bug
status: resolved
symptom: ld: undefined reference to sub_xxxxxxxx for a whole 1000-function chunk, though the chunk's .c defines every one of them; also 'RE-LIFT FAILED (a seed did not land)' and a json.decoder JSONDecodeError inside func_id
tags: xbox,tooling,recomp,build,concurrency
created: 2026-08-05
updated: 2026-08-05
---

## Symptom

Three unrelated-looking failures, all in one afternoon:

1. `ld.bfd: undefined reference to `sub_0020F450'` … 1000 distinct symbols, exactly the address range of generated chunk `recomp_0010.c` — a file that *does* define all 1000, and whose `.o` (checked with `nm -g --defined-only`) *does* export all 1000.
2. `xbox_discover.sh` round 3: `RE-LIFT FAILED (a seed did not land)`.
3. `func_id` dying in `json.loads` with a decode error.

## Cause

A **second `xbox_discover.sh` from a previous session was still running**. Both loops share `xbox/seeds.json`, the generated-C directory and the cmake build directory, and neither took a lock.

The tell was in `scratch/logs/xbox_discover.log`: rounds interleaved — `round 3` and `round 5` lines from two different drivers in one file, each truncating the log the other was writing. Confirmed with `ps -eo pid,ppid,etimes,args`: pid 919861 `bash ./tools/xbox_discover.sh` (note the `./` — a different invocation from this session's) plus its live `xbox_relift.sh` and `python3 -m tools.recomp` children.

Mechanism for each symptom:

- **(1)** The stray loop's compiler was still writing `recomp_0010.c.o` when this session's `make` decided the object was newer than its source, skipped it, and linked the half-written file. Nothing was wrong with the translation: a plain rebuild after the stray process finished linked clean, with no recompilation of that chunk.
- **(2)/(3)** One lift's `tools.recomp` / `tools.func_id` read JSON another lift was half-way through writing.

## Fix

Non-blocking `flock` guards, in the commit that records this:

- `tools/xbox_discover.sh` takes `scratch/.xbox_discover.lock` for the whole loop.
- `tools/xbox_relift.sh` takes `scratch/.xbox_lift.lock`, and additionally refuses when the discover lock is held by anyone other than its own parent loop (which passes `XBOX_DISCOVER_PID`).

Each refusal prints the holder's pid and start time and exits 1 — it never blocks and never proceeds. The lock file is opened `<>` rather than `>`: `>` truncates on open, which erased the holder line *before* `flock` reported the conflict, so the first version of the message said `holder: unknown` — a refusal that names nobody is the same lie as a silent one. Verified by running each of the three refusal paths against a held lock, and the accept path by lifting with no lock held.

## What to do when you see it again

`ps -eo pid,ppid,etimes,args | grep -E 'xbox_discover|xbox_relift|tools\\.(disasm|recomp|func_id)'` and kill the stray **by pid** (never `pkill -f`, which matches the shell running the grep). Then re-lift from clean before trusting any build.

**Do not chase an `undefined reference` in generated code as a translator bug until you have confirmed no other lift is running.**
