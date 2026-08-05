---
id: C075
kind: claim
status: holds
created: 2026-08-05
tags: xbox,recomp,flags,translation-defect
---

## Claim

GENUINE TRANSLATION DEFECT, now fixed: the recompiler never set the carry flag, so every MSVC sbb-sign idiom answered 'greater'. _cf was declared, initialised to 0 and never assigned, in 896 sites across 701 functions. MSVC emits 'sbb eax,eax / sbb eax,-1' to turn a byte cmp into -1/+1, which is how every inlined strcmp computes its sign; with CF stuck at 0 that always yields +1. A binary search over a string table therefore descends one way only and cannot find keys that are present.

## Evidence

grep over the generated tree: 896 'sbb self (CF extend)' sites in 701 functions, and a per-function scan found ZERO of those functions ever assigning _cf. Root case: sub_0026B390, the name-table binary search, whose miss on "DefaultFileName" produced the 0xFFFFFFFC tail-shift that ended every boot (C074). Fix: lifter materialises CF from the flag-writing instruction, per-function, only where sbb/adc consume it; instructions whose CF is not modelled emit a 'CF NOT MODELLED' marker and are counted rather than left looking correct. Regression test test_carry_flag_is_set_before_an_sbb_reads_it verified to FAIL on the pre-fix translator and PASS after.

## What would falsify it

A run showing the name lookup still missing keys that are in the table after the re-lift, which would mean CF was not the whole cause; or a 'CF NOT MODELLED' marker turning up on the path of a wrong result, which would mean the modelled subset is too small.
