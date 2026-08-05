---
id: C074
kind: claim
status: holds
created: 2026-08-05
tags: xbox,recomp,listscan
---

## Claim

The list-remove at 0x00275920 is translated FAITHFULLY; the run's fatal ~4GB memcpy is an UPSTREAM state divergence, not a translation defect at the crash site. The original x86 also computes the tail-shift length as ((count-1) - idx) << 2 and calls memcpy with it UNCONDITIONALLY, ignoring the find's return value. So when the find returns idx == count, the real Xbox binary underflows identically. The recompiled code is not adding the bug. What differs is that on the real console the name being removed IS in the table; in our run it is not.

## Evidence

Original bytes, tools/disasm/output/asm/text.asm 0x0027595B-0x00275977: mov eax,[esi+0x10]; mov edx,[esp+4]; lea ecx,[eax+edx*4]; mov eax,[esi+4]; dec eax; mov [esi+4],eax; sub eax,edx; shl eax,2; push eax; lea edx,[ecx+4]; push edx; push ecx; call 0x3d5890 -- no branch on the find result. Generated C in xbox/src/recomp/gen/recomp_0014.c matches instruction for instruction. Measured on the real run: scratch/logs/xbox_run_final.log, memcpy #413 dst=0x029021B4 src=dst+4 size=0xFFFFFFFC, preceded by find #821 name="DefaultFileName" count=409 idx=409.

## What would falsify it

A trace showing the same remove reaching this call site on real hardware or in an emulator with idx == count (which would make the original crash too, and move the divergence elsewhere); or finding that the caller sub_00289F50 is itself mistranslated so the remove should never have been reached.
