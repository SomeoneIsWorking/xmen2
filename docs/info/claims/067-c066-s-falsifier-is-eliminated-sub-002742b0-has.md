---
id: C067
kind: claim
status: holds
created: 2026-08-05
tags: xbox
depends: xbox/src/recomp/gen
---

## Claim

C066's falsifier is eliminated: sub_002742B0 has ZERO occurrences in the data sections, so no vtable reaches it and sub_002902C0's loc_00290557 really is the only route to the registry. The remaining explanation is that sub_00268BD0 is reached on a RETRY that the original does not take -- sub_00289F90 falls through to it only when the slot-0x1B0 call returns -1, which happens on a second attempt after the object's +0xA0 field is set.

## Evidence

A word-scan of .rdata and .data for 0x002742B0 finds nothing, while the same scan finds sub_002902C0 exactly once (at .rdata 0x004C52C0, vtable slot 40). In sub_00289F90 the guard is 'if (eax != 0xFFFFFFFF) goto loc_0028A62E' -- so a successful lookup leaves the function elsewhere and only a -1 falls through to sub_00268BD0, past a counter at esp+0x1C capped at 2. On the first call sub_0026C410 sees +0xA0 and +0xA4 both zero and takes its factory path.

## What would falsify it

if the original also retries here, the registry must be non-null by then through some route neither the xref scan nor the data scan can see, and this whole line is wrong
