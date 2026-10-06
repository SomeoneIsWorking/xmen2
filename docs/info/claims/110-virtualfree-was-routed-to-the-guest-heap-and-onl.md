---
id: C110
kind: claim
status: holds
created: 2026-08-06
tags: pc,jit,native,host,memory
---

## Claim

VirtualFree was routed to the guest heap, and only the heap's own refusal caught it

## Evidence

imp_KERNEL32_VirtualFree was . VirtualAlloc mmaps and never allocates from the guest heap, so every VirtualFree passed guest_free a pointer it had never issued. Identified by the address: 0x068bf000 is exactly a MEM_COMMIT address in the run's own [mem] log, and a malloc'd block sits inside a page rather than on the boundary; the boundary ring also showed no import crossing, because the guest was calling VirtualFree and not free. Now implemented properly -- MEM_RELEASE munmaps the whole reservation with the size taken from the reservation table (Win32 requires dwSize == 0) and refuses an address this host never reserved; MEM_DECOMMIT mprotects PROT_NONE so a use-after-decommit faults rather than reading stale data, while leaving the reservation so VirtualQuery still reports the range as the guest's. MEASURED: pairs entered 4958 -> 5016, battery 33/33. The defect survived because nothing released memory until the run reached shutdown, which it only did after the USER32 message pump landed.

## What would falsify it

MEM_DECOMMIT keeps the mapping and only removes access. A guest that decommits a large region expecting the physical pages back gets none, and nothing here reports that -- it would show up as memory pressure rather than as a failure. Whether the game decommits at scale is unmeasured.
