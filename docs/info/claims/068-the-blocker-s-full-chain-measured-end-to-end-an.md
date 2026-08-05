---
id: C068
kind: claim
status: holds
created: 2026-08-05
tags: xbox
depends: xbox/src/recomp/gen
---

## Claim

The blocker's full chain, measured end to end: an allocation inside the factory sub_0026E740 fails, so it exits via loc_0026E7BF returning -1; sub_0026C410 propagates that -1 (its own +0xA0/+0xA4 fields are zero on every call, so it always takes the factory path, never its own -1 exit); sub_00289F90 sees -1 and falls through to its retry, which calls sub_00268BD0; and that dereferences the engine registry at 0x0071037C, which sub_002902C0 has not built yet because it is still inside the slot-0x6C call one line earlier.

## Evidence

Breakpoints on all four exits of sub_0026E740 show the first one taken is loc_0026E7BF with eax=edi=0xFFFFFFFF -- the 'undo and return -1' path reached after an allocation through vtable slot 0xA4 returns neither the requested count nor -1. Tracing sub_0026C410 across six calls shows this=0x01084EE0 with +0xA0 and +0xA4 both zero every time. sub_002742B0 has zero occurrences in the data sections, so no vtable reaches the registry setter and the ordering cannot be fixed by finding another caller.

## What would falsify it

if slot 0xA4's allocator is failing because of the native heap override (C057) rather than a translation defect, XBOX_NATIVE_HEAP=0 would change the outcome -- untested
