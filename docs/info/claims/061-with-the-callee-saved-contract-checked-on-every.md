---
id: C061
kind: claim
status: holds
created: 2026-08-05
tags: xbox
depends: xbox/src/recomp_manual.c, xbox/xboxrecomp.lock
---

## Claim

With the callee-saved contract checked on EVERY call -- direct as well as indirect -- all 12,931 calls in a boot run restore ebx/esi/edi/ebp. The three apparent violations are conventions, verified against the binary rather than assumed away.

## Evidence

Extending the check to direct calls via a RECOMP_DCALL wrapper (test-first: the test asserted the emitted form and failed before the lifter change) took the checked count from 7642 to 12,931 and surfaced three targets. __SEH_prolog 0x003D62F4 and __SEH_epilog 0x003D632F rewrite the caller's frame by design. 0x003DC1B0 is _aulldvrm: its epilogue is 'edx = ebx; ebx = ecx; ecx = eax; eax = esi' before a single pop esi, so it RETURNS the remainder in ecx:ebx -- and 0x003DC1AF is 0xCC padding, so no push ebx is missing from the detected start.

## What would falsify it

the exemptions are hard-coded VAs for this title; another binary needs them re-derived, and a genuine violation at one of those three addresses would now be invisible
