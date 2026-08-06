---
id: C136
kind: claim
status: holds
created: 2026-08-06
tags: native,crt,setjmp,rc-native
---

## Claim

A setjmp slot may be reclaimed ONLY when its jmp_buf sits in a freed guest-heap block; the guest stack pointer says nothing about whether a buffer is still live

## Evidence

src/native/crt.c x86_setjmp_reclaim + jmp_slot_for; src/native/guest_heap.c guest_heap_addr_is_live. The table leaked: it took a slot per distinct jmp_buf address and never gave one back, so the game -- which calls FUN_00646abd once per resource load, each time taking a setjmp on a jmp_buf inside a freshly allocated object -- filled all 16 after 16 loads and aborted. The dump proved they were sequential and not nested: all 15 were recorded at the SAME guest esp 0x700ff6bc. The stack rule ('a jmp_buf below the current ESP is in popped space') was implemented, passed a unit check built to match it, and was REFUTED by the game: it freed slot 0's buffer at 0x700ff638 taken by FUN_006460d1 at 0x00646115, and the guest then longjmp'd to exactly that buffer from 0x0064608d, inside that same function -- 'the buffer was NEVER recorded'. Only the heap rule survives; anything not provably dead is kept and the table GROWS instead, saying so once. Six battery checks in case_setjmp_table cover both directions, including that a non-heap buffer is never reclaimed.

## What would falsify it

a longjmp reporting a jmp_buf that was never recorded, when that buffer had in fact been recorded earlier in the run -- that is a slot reclaimed while still live, and the heap rule would then be wrong too
