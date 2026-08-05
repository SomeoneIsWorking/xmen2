---
id: C044
kind: claim
status: holds
created: 2026-08-05
tags: xbox
depends: patches/xboxrecomp/0002-kernel-handle-and-ordinal-tables.patch
---

## Claim

A third shifted table: kernel_data_va_for_ordinal bound DATA exports one ordinal high, so three FUNCTIONS the game imports (ExFreePool 17, IoCreateDevice 65, XeLoadSection/XeUnloadSection 327/328) were handed a data pointer instead of a dispatch stub, and XeImageFileName/XboxLANKey/XboxAlternateSignatureKeys pointed at each other's storage.

## Evidence

Extending validate_ordinals.py to the data-export table names it row by row; after remapping by name the four tables (thunks, docs, bridge dispatch, arg sizes, data exports) all validate against the 371-entry export table. Separately, 28 arg-size rows were missing entirely for ordinals this game imports, and the default was 0 -- silently leaving arguments on the simulated stack; the default is now -1 and the bridge init prints every import whose cleanup size is unknown (5 remain, all DATA exports with no backing storage).

## What would falsify it

if a game imports ordinal 356/357 (HalBootSMCVideoMode, IdexChannelObject) and dereferences the thunk, the deliberate absence of those two rows becomes a null read
