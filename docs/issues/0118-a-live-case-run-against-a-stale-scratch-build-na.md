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

- `build/x2native` (root `cmake -S . -B build`), 85 MB, used by `ctest`
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

## Fix / how to avoid

Build `scratch/build-native` before any `tools/live_case.py` run:

    cmake --build scratch/build-native --target x2native -j$(nproc)

`ctest --test-dir scratch/build-native` also runs the full 107-test suite with
the locked python, which `build/` does not (its `pad_font` test fails on a
missing Pillow because the bare configure picks up the system interpreter).
