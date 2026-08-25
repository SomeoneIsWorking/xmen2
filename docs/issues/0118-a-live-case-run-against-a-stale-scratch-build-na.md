---
id: 118
title: A live case run against a stale scratch/build-native binary reads as evidence
status: resolved
symptom: tools/live_case.py PASSes checks for code that was never compiled into the binary it ran; a newly added override prints nothing and looks like it was never reached
tags: tooling,build,live_case,native,instrumentation
created: 2026-08-25
updated: 2026-08-25
---

## Symptom

`tools/live_case.py` launches `scratch/build-native/x2native` directly. A
session that had built the OTHER x2native -- the one the root `CMakeLists.txt`
produces in `build/` -- got a full PASS report describing behaviour of a
binary from an hour earlier, and a freshly added override at
`XMen2.exe 0x00402ba0` printed nothing at all.

## Root cause

Two build trees emit a target named `x2native`:

- `build/x2native` (root `cmake -S . -B build`), 85 MB, used by `ctest` -- now deleted and refused
- `scratch/build-native/x2native` (the locked initializer, RelWithDebInfo,
  `.venv` python), 273 MB, the one `run.sh`, `tools/live_case.py` and
  `tools/native_discover.sh` all run

`cmake --build build --target x2native` succeeds and changes nothing the live
harness will execute.

## What made it visible

Nothing in the PASS report. The override's own first-tick report did: it was
written to print on EVERY first entry, whichever branch it took, so
"no BOOT SPLASH line at all" was distinguishable from "the override declined".
An override that only spoke when it acted would have been indistinguishable
from one that was never compiled in, and the run would have been read as a
pass. This is the diagnostic-negative rule paying for itself.

## Fix

The duplicate is GONE rather than documented around. `build/` is deleted, and
the root `CMakeLists.txt` now refuses to configure into any in-source directory
that is not under `scratch/`, naming this issue and the commands that work. A
path outside the source root still configures, because that is what
`BUILD=<path>` in `tools/run.py` sets.

    ./run.sh                                       provision, build, launch
    cmake --build scratch/build-native -j$(nproc)  build without launching
    ctest --test-dir scratch/build-native --output-on-failure

The refusal was run against both classes before being trusted: `cmake -S . -B
build` fails with the message, while `scratch/build-native` and an out-of-tree
path both configure clean.

`scratch/build-native` was always the better tree anyway -- it runs the same
107 tests with the locked `.venv` python, which `build/` did not (its
`pad_font` failed on a missing Pillow because the bare configure picked up the
system interpreter, a failure with nothing to do with the code).
