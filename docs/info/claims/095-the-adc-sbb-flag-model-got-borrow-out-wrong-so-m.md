---
id: C095
kind: claim
status: holds
created: 2026-08-06
tags: pc,recomp,translator,correctness,flags
---

## Claim

The ADC/SBB flag model got borrow-out wrong, so MSVC's sign idiom returned 'equal' and every binary search over a string table found the wrong entry

## Evidence

tools/recomp.py emitted ADC/SBB by squeezing the carry-in into the lazy (a,b,r) triple: SETFLAGS(FK_SUB, a, b - c, r, w) for SBB. FLAG_C for FK_SUB is (a&m) < (b&m), so for SBB EAX,EAX with CF=1 and EAX non-zero it asks EAX < EAX-1 -> false, where x86 gives borrow-out = borrow-in = 1. MSVC's sign idiom 'sbb eax,eax; sbb eax,-1' therefore computed -1 + 1 - 0 = 0 -- EQUAL -- instead of -1 for every mismatch where eax was non-zero. libIGCore's string pool interns via a BINARY SEARCH over a sorted table ending in exactly that idiom (FUN_1004f770), so a mismatch read as a hit: MEASURED before the fix, setString('igObject') and setString('_refCount') both returned 0x00a82b88. Field names were therefore never bound -- the four fields on __internalObjectList's meta all carried class names -- so getMetaField('_data') returned NULL and arkRegisterInitialize dereferenced it (issue #16). Note 'b + c' is not a fix either: it wraps when b = 0xFFFFFFFF. FIXED by computing real flags at the instruction (x86_flags_adc / x86_flags_sbb -> an EFLAGS word for FK_EXPLICIT). VERIFIED four ways: tests/test_flags.c has 22 known-answer checks and the same idiom case was run against a reimplementation of the OLD model, which yields 0x00000000 where x86 gives 0xffffffff, so the test discriminates rather than merely passing; setString now returns distinct pointers (0x00a82b88, 0x00a82b9c, 0x00a82bb0, ...) where it previously collapsed; the arkRegisterInitialize fault is gone; distinct (entry point, module) pairs entered rose 1548 -> 1611. Battery 33/33.

## What would falsify it

The new helpers are exercised by 22 known-answer cases at byte and dword width, not by a differential run against the original DLL. Word width (w=2) has no case at all, and the shift/rotate instructions still set CF through a separate path that this change did not touch -- if a 16-bit ADC/SBB or a rotate-derived carry misbehaves, this fix does not cover it.
