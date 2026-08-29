---
id: C060
kind: claim
status: holds
created: 2026-08-05
tags: xbox
depends: xbox/xboxrecomp.lock
---

## Claim

NtQueryVirtualMemory was sized as 4 args and had no bridge at all. The wrong size over-popped 8 bytes per call -- 504 of 505 stack-balance violations -- and the missing bridge left RegionSize zero, which made the caller's address-space walk spin over two billion times.

## Evidence

Two new instruments found it. The callee-saved contract check reported 'sub_0027BEF0 did not restore esi: 0x00560D84 -> 0x00000000', and the ESP bounds check reported esp leaving the guest stack after the thunk at 0xFE000124 = slot 73 = ordinal 217. Sizing it 8 bytes (the Xbox 2-arg form, not the 6-arg NT one) and bridging it against our own memory layout gives: 7642 indirect calls checked with every one restoring ebx/esi/edi/ebp, esp inside the stack for every call, and the title allocating an 8 MB block instead of spinning.

## What would falsify it

the region table is the loader's layout, not the guest's real page state; a title that queries a page we report as committed but never mapped will get a wrong answer that the instruments cannot see
