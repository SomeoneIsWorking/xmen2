---
id: C043
kind: claim
status: holds
created: 2026-08-05
tags: xbox
depends: patches/xboxrecomp/0002-kernel-handle-and-ordinal-tables.patch
---

## Claim

The xboxrecomp kernel bridge bound 45 ordinals to the wrong function: 6 dispatch entries and 39 stdcall arg-size rows were shifted (mostly by one) against the validated 371-entry export table -- the same defect validate_ordinals.py was written for, in the two tables it never checked.

## Evidence

Extending validate_ordinals.py to cover kernel_bridge.c's dispatch and stdcall_args_for_ordinal tables reported 45 mismatches, each naming the ordinal the function actually belongs at; after remapping every row to its name's canonical ordinal the validator reports OK across 193 thunk cases, 131 doc rows, 55 bridge dispatches and 130 arg-size rows. Concretely: the game's ordinal 47 is HalRegisterShutdownNotification but ran HalReadSMCTrayState, which wrote 0x10 and 0 through two of its arguments and had a 24-byte instead of 8-byte stdcall cleanup.

## What would falsify it

if the xbe_parser export table is itself wrong, every one of these 'fixes' is a regression -- it is trusted because kernel_thunks.c, which binds the game's actual imports, already validates against it
