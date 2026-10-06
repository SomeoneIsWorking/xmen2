---
id: C189
kind: claim
status: falsified
created: 2026-08-14
tags: input,xbox
falsified_on: 2026-08-20
---

## Claim

Xbox per-player controller vtable 0x004A9D6C slot +0x10 IS the physical-value getter sub_0015F5B0, which returns the float at [this + index*4 + 0x2fc]; slot +0x38 (sub_0015F5C0) is its setter, and the array holds exactly 30 entries (+0x2fc..+0x373, abutting the previous-digital-mask word at +0x374). The class constructor sub_0015F460 also builds 30 binding records of 0x14 bytes at +4 through the MSVC vector-ctor iterator sub_003D4640 (array, elemsize=0x14, count=0x1e, ctor=sub_0015F310, dtor=0x3cf930), so 30 is the physical-source count on both sides of the object. C188 inferred this slot; it is now resolved from the vtable image.

## Evidence

tools/xbe_query.py read 0x004A9D7C (slot dword = 0x0015F5B0), vtable 0x004A9D6C --count 20, func 0x15F5B0 / 0x15F5C0 / 0x15F460 / 0x3D4640, and find 0x0015F5B0 (exactly 1 occurrence in 5,681,344 bytes across 21 sections -- the slot itself). xbe_query.py selftest passes 8 checks, each with its negative control.

## What would falsify it

a second vtable containing sub_0015F5B0, or a controller instance whose float array is written past +0x373

## FALSIFIED 2026-08-20

The +0x10 physical-float getter and 30-float extent hold, but +0x38 is a byte getter at +0x2d8, not the float setter; +0x2c is the setter.

> Anything that cited this claim as proof must be re-checked. Grep the repo for it.
