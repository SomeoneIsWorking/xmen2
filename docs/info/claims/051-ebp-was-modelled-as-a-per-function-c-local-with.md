---
id: C051
kind: claim
status: holds
created: 2026-08-05
tags: xbox
depends: xbox/src/recomp_types.h
---

## Claim

ebp was modelled as a per-function C local with a g_seh_ebp bridge; it is now a global register like every other. Behaviour-neutral on the current run, but it removes the bridge and the class of bug where FPO code carries a live value in ebp across an ordinary call.

## Evidence

Generated C no longer declares 'uint32_t ebp' or emits 'ebp = g_seh_ebp' (4998 functions had the latter), and the SEH prolog/epilog need no special casing. Diffing the kernel/ICALL/FILE trace of the run before and after: identical, so the change is verified not to regress. sub_00208950 now loads ebp from its argument via PUSH32/POP32 like ebx, esi and edi.

## What would falsify it

a function that clobbers ebp without saving it now destroys its caller's ebp -- which is what the hardware does, but if some lifted function relied on the old isolation it will break
