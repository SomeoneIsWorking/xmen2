---
id: C016
kind: claim
status: holds
created: 2026-08-04
tags: 
---

## Claim

116 recompiled functions are verified against the original under FORCED RELOCATION, comparing memory writes as well as return values, and that set runs in the game rendering real 3D scenes.

## Evidence

difftest reserves 0x10000000 so the DLL must relocate (observed 0x00c20000), gives the original and the recompiled code two 1MB regions filled with identical bytes (so functions that index memory by their argument read matching bytes on both sides), and compares BOTH the masked return value and the full 1MB region afterwards. 128 cases -> 116 verified, 1 failed, 11 untestable. Return width is taken from the MSVC mangled return type, so bool/char returns are not judged on undefined upper EAX bits. The 116-function hybrid DLL runs the game to a rendered snowy 3D scene.

## What would falsify it

1 case genuinely fails (igTNonRefCountedObjectStack::setTop writes through a pointer STORED in the object, so both sides write to the same absolute address and mirroring cannot work) and 11 are untestable because random objects never produce valid input -- neither is verified, and both are excluded from the DLL. Arguments are masked to 0xFFF to keep index-shaped uses in bounds, so large-value arithmetic is under-exercised.
