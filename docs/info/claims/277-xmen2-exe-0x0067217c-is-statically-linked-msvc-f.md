---
id: C277
kind: claim
status: holds
created: 2026-09-03
tags: 
depends: src/native/crt_static_overrides.c
---

## Claim

XMen2.exe!0x0067217c is statically-linked MSVC _ftol2 and is now handled by a native override (src/native/crt_static_overrides.c) reusing x87_crt_ftol; the ~2.8% guest-wall block cluster 0x006721xx is gone from the in-game execution profile

## Evidence

disassembly at 0x67217c matches the canonical _ftol2 CW-independent float->int64 truncation pattern; crt_static_overrides unit test proves trunc-toward-zero + edx:eax + cdecl esp; driven in-game run 2026-09-03 (scratch/logs/ftol2_on.log): 'overrides: 80 bound', no resolve abort, 0x006721xx absent from top-40 execution-weighted blocks (was #8-11 ~2.8%), gameplay screenshot correct

## What would falsify it

the 0x006721xx blocks reappear in a jit.profile top-N, or a driven run shows corrupted screen/camera coordinates, or the override fails to resolve
