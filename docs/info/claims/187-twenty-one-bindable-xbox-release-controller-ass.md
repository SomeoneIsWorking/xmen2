---
id: C187
kind: claim
status: holds
created: 2026-08-14
tags: xbox,controller,input,re
---

## Claim

Twenty-one bindable Xbox-release controller assignments align with the PC binding engine. Automatic installation preserves existing pad mappings; explicit Xbox Defaults replaces the pad slot and persists as user-selected state.

## Evidence

Xbox default.xbe `sub_00162240` action constructor and options_controller_xbox controller screen; PC XMen2.exe FUN_00619c40 action switch, FUN_0061b030 42-row loader, FUN_006281f0 physical-code naming, FUN_006297a0 setter, and FUN_0061dc10/FUN_006188c0 editor buttons; tests/test_xbox_defaults.c shipping-wrapper test. The real-path gate `X2_VIRTUAL_PAD=1 X2_UNPACED=1 X2_MAX_FRAMES=1800 RUN_ARGS='--no-window' timeout 120 ./run.sh` installed all 21 rows through retained FUN_006297a0, presented 1,802 frames with 1,918 draws and zero refused, reached the frame cap, and exited 0.

## What would falsify it

A runtime dump from the Xbox release shows a different physical-to-action assignment for any included control, either relabelled button does not invoke the tested shipping wrapper in the real editor, or the wrapper test fails its exact-tuple, label, callback-ABI, custom-map, explicit-ownership, idempotence, or disconnect checks.
