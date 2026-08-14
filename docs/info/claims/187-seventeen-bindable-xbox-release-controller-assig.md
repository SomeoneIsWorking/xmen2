---
id: C187
kind: claim
status: holds
created: 2026-08-14
tags: xbox,controller,input,re
---

## Claim

Seventeen bindable Xbox-release controller assignments align with the PC binding engine and install through its retained setter without overwriting existing pad mappings.

## Evidence

Xbox default.xbe action registry and options_controller_xbox controller screen; PC XMen2.exe FUN_00619c40 action switch, FUN_0061b030 42-row loader, FUN_006281f0 physical-code naming, and FUN_006297a0 setter; tests/test_xbox_defaults.c shipping-wrapper test. The real-path gate `X2_VIRTUAL_PAD=1 X2_UNPACED=1 X2_MAX_FRAMES=1800 RUN_ARGS='--no-window' timeout 120 ./run.sh` installed 17 rows through FUN_006297a0, presented 1,803 frames, and exited 0 at the frame cap.

## What would falsify it

A runtime dump from the Xbox release shows a different physical-to-action assignment for any included control, or the shipping wrapper fails its exact-tuple, custom-map, idempotence, or disconnect checks.
