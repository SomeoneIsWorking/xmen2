---
id: C182
kind: claim
status: holds
created: 2026-08-14
tags: launcher,native,sdl3
depends: run.sh, bootstrap.py, tools/run.py, src/native/x2native_options.c
reconfirmed: 2026-08-24
verified_at: 2026-08-24 22:11:46
---

## Claim

Zero-argument run.sh and x2native launch the current native SDL3 GPU plus D3D8 game target; run.sh has no command or argument surface.

## Evidence

`tests/test_x2native_options.c` exercises the executable's zero-argument product policy. `tests/test_launcher.py` requires the shell shim to reject arguments and requires the bootstrap to exec the locked interpreter with only `tools/run.py`. For the cold-path observation, `.venv`, `vendor/shared`, `scratch/recomp`, generated `src/recomp/*.c`, the prompt asset pack and `scratch/build-native` were all moved aside. `X2_MAX_FRAMES=10 ./run.sh` then recreated every input without Ghidra, configured Clang 22.1.8 and the uv Python 3.14.7 interpreter, linked all twenty modules, armed host D3D8, entered XMen2.exe, presented 18 frames, and exited at the cap. The full CTest suite passed 100/101 with the media decoder test explicitly skipped for its missing optional fixture.

## What would falsify it

A no-argument cold launcher run that needs Ghidra or a sibling checkout, does not link all twenty modules, fails to arm host D3D8 and enter XMen2.exe, or accepts a launcher argument/mode.

## Re-confirmed 2026-08-24

Cold X2_MAX_FRAMES=10 ./run.sh recreated uv/shared/generated/build inputs, linked 20 modules with Clang, presented 18 frames, and exited; launcher and full 101-test CTest gate passed.
