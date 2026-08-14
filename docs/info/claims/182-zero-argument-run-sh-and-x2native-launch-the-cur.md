---
id: C182
kind: claim
status: holds
created: 2026-08-14
tags: launcher,native,sdl3
---

## Claim

Zero-argument run.sh and x2native launch the current native SDL3 GPU plus D3D8 game target, while optional run arguments extend rather than replace that target

## Evidence

tests/test_x2native_options.c exercises zero args, --no-window, explicit renderer-free --run, and --selftest through the shipping parser. A real X2_MAX_FRAMES=10 RUN_ARGS=--no-window ./run.sh run logged host Direct3D8 armed, XMen2.exe entry, frames presented, zero refused draws, and clean stop at the cap in scratch/logs/run-default.log.

## What would falsify it

A no-argument launcher run that does not log both host Direct3D8 armed and XMen2.exe entry, or an optional RUN_ARGS value that suppresses the D3D8 product route.
