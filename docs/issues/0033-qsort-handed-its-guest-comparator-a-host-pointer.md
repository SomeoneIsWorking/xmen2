---
id: 33
title: qsort handed its guest comparator a HOST pointer, and the crash landed in a scene-graph comparator
status: resolved
symptom: *** SIGSEGV at 0xf7832c60 (not an import slot) in fn_libIGSg_1005e3a0, a sort comparator, at FLD float ptr [ECX+0x18]
tags: pc,native,crt,qsort,guest-memory,rc-native
created: 2026-08-06
updated: 2026-08-06
---

## Symptom

The native run faults at a HIGH address -- `0xf7832c60`, nowhere near the guest's 32-bit space -- inside `fn_libIGSg_1005e3a0`:

    1005e3ae  MOV ECX,dword ptr [EAX + EDX*0x4]
    1005e3b5  FLD float ptr [ECX + 0x18]        <-- faults

which reads as a corrupt global table (`EAX` comes from `[[0x10151bd0]+0x10]`) and sends you looking at scene-graph initialisation.

## Cause

`FUN_1005e3a0` is a **qsort comparator**. It takes two pointers, dereferences each to get an index, and looks the objects up in a table. `imp_MSVCR71_qsort` in `crt.c` held the element being inserted in a **host `malloc`** buffer and passed its address to the comparator:

    unsigned char *tmp = malloc(sz);
    ...
    WR32(K.esp + 4u, (uint32_t)(uintptr_t)tmp);   /* truncated to 32 bits */

On a 64-bit host that pointer is above 4 GB, so the guest received its low half and dereferenced whatever happened to be there. `0xf7832c60` is the bottom of a host heap address.

The bug is invisible to any comparator that only COMPARES its two pointers, and fatal to any that dereferences one -- which is most of them.

## The rule it broke

Anything the guest dereferences must live below 4 GB, in `guest_malloc` memory. The D3D8 layer follows this for its staging buffers and says so; `qsort` was the one place that did not.

## Fix

`guest_malloc(sz)` / `guest_free`, and four battery checks (`case_qsort`) that drive a real comparator through `x86_guest_call` and assert **both** arguments are inside the guest heap before dereferencing them. Passing a host pointer to just the comparator fails 3 of the 4.
