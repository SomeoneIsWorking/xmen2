---
id: C066
kind: claim
status: holds
created: 2026-08-05
tags: xbox
depends: xbox/src/recomp/gen
---

## Claim

The registry NULL is not a single last wall: a diagnostic experiment that skipped sub_00268BD0 while 0x0071037C is NULL bought 2 more indirect calls (7648 -> 7650) before stopping on a fresh unresolved indirect call. The dispatch chain into it is correct, so the ordering assumption is what differs from the original, not the routing.

## Evidence

Slot 0x6C on the object at 0x01084EE0 (vtable 0x004C55B8) resolves to sub_0026BB80, matching the crash backtrace, and slot 0x1B0 resolves to sub_0026C410 which returns -1 only after its 64-bit field at +0xA0 has been set -- on the first call both halves are zero and it takes the factory path. So every dispatch on the path is right, and sub_002902C0 legitimately reaches its slot-0x6C call before the loc_00290557 line that sets the registry. The experiment was reverted; the tree carries no part of it.

## What would falsify it

if some OTHER caller of sub_002742B0 exists that the static xref scan missed -- an indirect one -- the ordering is fine and the missing step is that call
