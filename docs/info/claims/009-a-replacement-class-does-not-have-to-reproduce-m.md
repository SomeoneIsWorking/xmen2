---
id: C009
kind: claim
status: holds
created: 2026-08-04
tags: 
---

## Claim

A replacement class does NOT have to reproduce MSVC's vtable placement: Alchemy captures the vtable pointer through a per-class retrieveVTablePointer hook and libIGCore stamps it into every instance, so hand-rolled C vtables suffice.

## Evidence

igWin32Window::retrieveVTablePointer @0x10004040 builds a throwaway instance, writes &_vftable_ into it, and reads the pointer back at an offset igArkCore stores at +0x394 -- i.e. the vptr offset is discovered at runtime, not assumed. Abstract classes pass NULL for this hook; concrete ones supply it (igWindow/igControllerManager NULL, igWin32Window supplied).

## What would falsify it

This covers the vtable POINTER only. Slot ORDER inside the vtable is untouched by it -- callers that dispatch virtually index by slot, so a wrong order still breaks. That layout has not been read out of the binary yet.
