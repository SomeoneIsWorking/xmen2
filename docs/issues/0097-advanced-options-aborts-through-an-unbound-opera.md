---
id: 97
title: Advanced Options aborts through an unbound operator delete[] import
status: resolved
symptom: Pressing Space on Options -> Advanced Options aborts x2native immediately after the menu action.
tags: pc,native,crash,menu,crt,imports,abi,user-report
created: 2026-08-22
updated: 2026-08-22
---

## Observation

The exact-input recording `scratch/recordings/input-20260822-093706-2867467.jsonl` ends without an end record after DIK 0x39 (Space) at frame 5599. The retained PID 2867467 core is SIGABRT in `x86_missing_import("MSVCR71.dll", "??_V@YAXPAX@Z")`, reached from XMen2.exe 0x00619ac0 through 0x0061dc10, the builder that later pushes the retail string `Advanced Options`.

## Root cause

The translator canonicalizes `??_V@YAXPAX@Z` as `imp_MSVCR71____V_YAXPAX_Z`. `crt.c` defined `imp_MSVCR71___V_YAXPAX_Z`, one underscore short, so the strong guest-free body and the generated weak abort stub were different ELF symbols. The MSVCRT alias repeated the same spelling error. This is a CRT import-binding defect exposed by the temporary retail menu, not menu or Space-key behavior.

## Resolution

Correct the MSVCR71 implementation and MSVCRT alias token to the canonical four-underscore spelling. `crt_selftest` invokes the shipping XMen2.exe delete[] thunk at 0x00672538, so the generated canonical import route, cdecl stack delta, and guest-heap release are all regression-checked. The existing CRT ABI checks moved into the same cohesive owner.
