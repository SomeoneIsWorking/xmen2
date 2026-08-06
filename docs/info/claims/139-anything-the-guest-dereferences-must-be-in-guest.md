---
id: C139
kind: claim
status: holds
created: 2026-08-06
tags: native,crt,guest-memory,rc-native
---

## Claim

Anything the guest dereferences must be in guest memory, and qsort's held-out element was the last place that was not

## Evidence

src/native/crt.c imp_MSVCR71_qsort. The comparator is guest code handed two POINTERS it dereferences; the held-out element was a host malloc truncated to 32 bits, so the guest read the low half of a host heap address. Observed as SIGSEGV at 0xf7832c60 in fn_libIGSg_1005e3a0 (a comparator that does MOV ECX,[EAX+EDX*4] then FLD [ECX+0x18]) -- named by addr2line, not guessed. Fixed with guest_malloc; 4 battery checks in case_qsort drive a real comparator through x86_guest_call and validate both arguments with guest_heap_contains BEFORE dereferencing, so a bad pointer reports instead of faulting. Mutation-proved: passing a host pointer to the comparator alone fails 3 of the 4 checks.

## What would falsify it

a guest callback that receives an address from this host and dereferences it successfully when that address is not in the guest heap -- that would mean the 4 GB constraint is not real and this rule is over-strict
