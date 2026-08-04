---
id: C026
kind: claim
status: holds
created: 2026-08-05
tags: 
---

## Claim

The recompiled XMen2.exe now runs 43+ functions deep into the CRT startup, executing real recompiled code, after three loader defects were fixed. The remaining blocker is Ghidra's incomplete function identification, not the translation.

## Evidence

Call history shows 43 distinct recompiled functions entered (FUN_00601a40, FUN_0055dad0, FUN_0055e080, ... repeating, i.e. real loops) before stopping. Three fixes got it there: (1) RET now honours a popped address that differs from the entry address, because __SEH_prolog rewrites its own; (2) the mapped image's IAT was still holding hint/name RVAs from the file -- 989 slots are now patched with resolved addresses, which is what 0x002d28a8 actually was; (3) indirect calls landing outside the guest image are host calls and now route through x86_call_host, which is how the CRT reaches GetModuleHandleA. It now stops at 0x00597b30, which IS inside the image but is not a function Ghidra identified.

## What would falsify it

This is CRT startup, not game code -- no engine call, no window, no frame. And the stopping point moves with each fix, so the count of functions reached says nothing about how much further there is to go. The next blocker class (unidentified functions inside the image) needs an iterative loop: collect missed targets at runtime, add them as function starts in Ghidra, re-export, re-recompile.
