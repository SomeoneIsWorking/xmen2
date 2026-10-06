---
id: 30
title: The setjmp buffer table never gave a slot back, so 16 resource loads aborted the run
status: resolved
symptom: crt: more than 16 live setjmp buffers. Refusing rather than dropping one -- a dropped buffer is a longjmp that lands nowhere.
tags: pc,native,crt,setjmp,rc-native,rc-exe
created: 2026-08-06
updated: 2026-08-06
---

## Symptom

`x2native --d3d8 --run` aborts in `crt.c`:

    crt: more than 16 live setjmp buffers. Refusing rather than dropping one.

It appeared only once the renderer got far enough to load resources, which is why it looks like a graphics bug and is not one.

## Cause

`jmp_slot_for` allocated a slot per DISTINCT jmp_buf address and NOTHING ever set `used` back to 0. A slot came back only if the identical address was passed again. The game calls `FUN_00646abd` once per resource load; each call allocates an object and takes a setjmp on a jmp_buf INSIDE it, at a fresh heap address. Sixteen loads, sixteen slots, abort.

The message could not distinguish that from a legitimately 17-deep nest, which is its own defect: it printed the count and nothing else. It now dumps the table -- each env, whether it is above or below the current ESP, the guest ESP recorded at the setjmp, and the calling body. That dump is what settled it: all fifteen were recorded at the SAME guest esp `0x700ff6bc`, so they were sequential calls at one stack depth, not nested frames.

## The rule that was wrong -- a DEAD END worth keeping

Two reclamation rules were implemented:

1. a jmp_buf in a guest-heap block that has been freed cannot be jumped to; and
2. a jmp_buf on the guest stack BELOW the current stack pointer is in popped space.

(2) is plausible and WRONG. It passed the battery check written for it -- which was built from the same reasoning, so it could only agree -- and the real run refuted it inside one minute: it reclaimed slot 0's buffer at `0x700ff638`, taken by `FUN_006460d1` at `0x00646115`, and the guest then longjmp'd to that exact buffer from `0x0064608d` **inside that same function**, producing `longjmp(0x700ff638, 1) -- the guest is unwinding to a setjmp this host cannot resume`. An address below ESP is not evidence that the frame owning it has returned.

Only rule (1) survives. Anything not provably dead is KEPT, and if every slot is live the table grows (doubling, capped at 4096) and says so once -- silent growth is how a leak comes back unnoticed. In this game the reclaimer frees nothing, because those objects are never freed; the growth path is what carries the run.

## What made the difference

Running the discriminator against the real corpus rather than against the case built to match it. The battery check for rule (2) has been replaced by its opposite -- 'a non-heap buffer is never reclaimed' -- with the refutation written next to it.

## Fixed in

`src/native/crt.c` (reclamation, growth, the table dump), `src/native/guest_heap.c` (`guest_heap_addr_is_live`), `src/native/x2native.c` (`case_setjmp_table`, 6 checks). C136.
