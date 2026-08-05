---
id: 5
title: Every inlined strcmp returned 'greater': the carry flag was never set
status: resolved
symptom: A name/string lookup misses a key that IS in the table; a binary search over strings behaves as if every comparison went the same way. Downstream: a list remove computes a tail-shift length of 0xFFFFFFFC and memcpy walks off the heap
tags: xbox,recomp,flags,translation-defect,strcmp
created: 2026-08-05
updated: 2026-08-05
---

## Symptom

The boot died in the CRT memcpy with a ~4 GB length, called from the list
remove at 0x00275920, because a lookup for `"DefaultFileName"` returned
`idx == count` -- not found -- and the remove shifted anyway. Both the remove
and its caller were verified FAITHFUL against the original bytes (C074), so the
divergence had to be in the lookup.

## Cause

`sub_0026B390` is a binary search over a name table. Its string compare ends in
MSVC's sign idiom:

    cmp  cl, byte ptr [esi]
    jne  0x26b404
    ...
    0x26b404:  sbb  eax, eax        ; eax = CF ? -1 : 0
               sbb  eax, -1         ; -> -1 or +1

`sbb` reads **CF itself**. The lifter's flag model is lazy: a `cmp` snapshots
its operands into `_flg0/_flg1` and the following `jcc` recomputes the
condition from them. That covers every conditional jump -- including the
unsigned ones -- so the model looked complete. But nothing ever set CF.
`_cf` was declared, initialised to `0`, and never assigned:

    int _cf = 0;                                  /* declared */
    eax = _cf ? 0xFFFFFFFF : 0;                   /* read     */
    eax = eax - 0xFFFFFFFFu - _cf;                /* read     */

With CF pinned at 0 the idiom always computes `0 - (-1) - 0 = +1`, so **any
mismatch reported "greater"**. The binary search could only ever descend one
way and failed to find keys that were present.

**896 sites across 701 functions** read `_cf`; a per-function scan found not one
of them assigning it. Every inlined string compare in the game was affected.

## Fix

The lifter now materialises CF from the flag-writing instruction, using the
same operand snapshots the branch uses so CF and the branch cannot disagree.
It is per-function -- only where `sbb`/`adc` actually consume CF -- so the
~25k functions that never read it get no extra code.

Exact for `cmp`, `sub`, `add`, `test`/`and`/`or`/`xor` (clear CF) and `neg`.
Note `sub`/`add`/`neg` have already written their destination when the
assignment is emitted, so their CF is expressed in terms of the RESULT.
Shifts, rotates, mul/div and the bit instructions also write CF and are NOT
modelled -- they emit a `/* CF NOT MODELLED after <mnemonic> */` marker and are
counted, rather than leaving a stale `_cf` that reads exactly like a correct
one.

## Verification

`test_carry_flag_is_set_before_an_sbb_reads_it` in test_regressions.py was run
against BOTH classes: it FAILS on the pre-fix translator ("_cf is read but
never assigned") and passes after. A second test asserts that a function with
no sbb/adc gets no CF code at all, so the per-function scoping cannot silently
become global.

## Why it hid for so long

C022 suspected the flag model, was investigated with a 54-case fuzzer over a
DIFFERENT find function, found zero mismatches, and was falsified as
"overstated" -- with a note that the lazy-flag model should not be assumed
correct just because 156 functions passed. That note was right. The fuzzer
exercised the `jcc` path, which was fine; nothing exercised an `sbb` reading
CF.
