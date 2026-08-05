---
id: C064
kind: claim
status: holds
created: 2026-08-05
tags: xbox
depends: xbox/src/recomp/gen
---

## Claim

sub_002902C0, which builds the engine registry at 0x0071037C, is slot 40 (offset 0xA0) of the 66-slot vtable at 0x004C5220. It is reached only by a virtual call through that slot, which the run never makes -- so the missing step is whatever should invoke vtable+0xA0 on that object.

## Evidence

sub_002902C0 has no static callers and appears exactly once in the data sections, at .rdata 0x004C52C0. Walking the run of consecutive code pointers around it gives the vtable 0x004C5220..0x004C5324, putting it at slot 40. Breakpoints on both writes to 0x0071037C never fire before the fault, and the indirect-call tally is clean, so the slot is simply never called.

## What would falsify it

if a DIFFERENT object's vtable also carries this function at another offset, slot 40 is the wrong lead -- the single .rdata occurrence is the evidence against that
