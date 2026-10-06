---
id: C236
kind: claim
status: holds
created: 2026-08-22
tags: input,controller,hotswap,identity,tests
depends: src/input/controller_instance.c#x2_controller_instance_resolve, src/input/controller_hotplug.c#x2_controller_hotplug_needs_admission, src/native/dinput_pad.c#dinput_pad_for_persistent_id, tests/test_dinput_pad.c#main
reconfirmed: 2026-08-22
verified_at: 2026-08-22 13:39:08
---

## Claim

Controller reconnect identity and admission are not bounded by the eight-pad simultaneous inventory limit

## Evidence

test_controller_instance uses the production immutable-GUID owner to prove an old object remains unresolved when a new GUID reuses slot zero. test_controller_hotplug drives twelve attach/detach generations and proves exactly one admission per change plus zero on unchanged polls. test_dinput_pad drives twelve real SDL virtual attach/detach cycles, proving generation increments, old GUID death, distinct replacement GUIDs, and that identical devices without serial/path receive session identities which never satisfy persistent reservations.

## What would falsify it

A production-path test shows a disconnected guest object resolves to a replacement in the same slot, or an unchanged generation re-admits, or any reconnect after eight lifetimes fails to receive exactly one admission.

## Re-confirmed 2026-08-22

test_controller_instance proves immutable live-GUID resolution; test_controller_hotplug drives twelve changed generations with exactly one admission each; test_dinput_pad drives twelve SDL virtual attach/detach cycles, proving old GUID death, fresh replacement GUIDs, and that two identical units without serial/path receive session identities which never satisfy persistent reservations.
