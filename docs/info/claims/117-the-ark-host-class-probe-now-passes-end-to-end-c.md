---
id: C117
kind: claim
status: holds
created: 2026-08-06
tags: ark,vulkan
---

## Claim

The ARK host-class probe now PASSES end to end, closing C008's falsifier: libIGCore registers, allocates and fully constructs a class defined in host C, and the constructed object dispatches through the host's own vtable. Verified positively rather than by absence of a crash -- meta+0x48 holds the instance size we passed, meta+0x1a is 0, createInstance returns non-NULL, and the object's vptr equals the exact vtable address handed to retrieveVTablePointer. What unblocked it was C116: igObject's 21 virtuals are 17 do-nothing stubs, so implementing the inherited interface is copying what igObject itself does, not stubbing over it.

## Evidence

scratch/build-native/x2native --no-window --ark-probe against the real install prints: meta object allocated 0x00a9b760; meta+0x48 instance size 0x10; meta+0x1a isAbstract 0; createInstance returned 0x00a9b828; object vptr == our vtable 0x7101bc38; PROBE PASSED. 20 of 64 vtable slots implemented, the remaining 44 still report by index if dispatched.

## What would falsify it

A class with real fields or a deeper base failing where this succeeded -- igVkProbe derives straight from igObject and has no meta fields, so it does not exercise instantiateAndAppendFields or a multi-level parent chain, both of which igVisualContext needs.
