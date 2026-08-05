---
id: C071
kind: claim
status: holds
created: 2026-08-05
tags: xbox
---

## Claim

C070 is fixed and its falsifier eliminated: honouring the placed base DOES get sub_0026E740 past its -1 exit. The fix is that the two memory bridges now describe the same address space -- NtQueryVirtualMemory reports as MEM_FREE only the arena's unused tail (everything at or above the 64 MB limit is now MEM_RESERVE, since no reservation there could be backed), and NtAllocateVirtualMemory satisfies a placed reservation through xbox_HeapReserveAt, which returns the requested address or nothing. A refusal returns STATUS_CONFLICTING_ADDRESSES so an address-space walker retries higher instead of aborting.

## Evidence

Same binary, one change. Before: '[KERNEL] PLACED reserve at 0x04000000 size=8323072: returning 0x02900000 instead', sub_0026E740 -> 0xFFFFFFFF, fault at 0x81ED8BCD, 7649 indirect calls. After: '[HEAP] #6: PLACED 0x02900000..0x030F0000', '[KERNEL] PLACED reserve at 0x02900000 -> granted at the requested address', and XBOX_ICALL_WATCH shows sub_0026E740 returning 0x02900000 and sub_0026C410 propagating it. 7724 indirect calls, and the 0x81ED8BCD registry fault is gone. The first call, which places at 0x01085008 (below the bump pointer), is still correctly refused.

## What would falsify it

if a later run needs more than the arena tail can hold, the 64 MB flat model is the next wall and this becomes a bound rather than a fix -- watch for 'placed reserve REFUSED: it does not fit'
