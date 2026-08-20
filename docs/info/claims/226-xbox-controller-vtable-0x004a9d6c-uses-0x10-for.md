---
id: C226
kind: claim
status: holds
created: 2026-08-20
tags: input,xbox
depends: tools/xbe_query.py#cmd_vtable
---

## Claim

Xbox controller vtable 0x004A9D6C uses +0x10 for the 30-float getter, +0x24 for logical-bit set/clear, +0x2c for the float setter, and +0x38 for a byte getter

## Evidence

default.xbe vtable bytes and disassembly of sub_0015F5B0/sub_0015F5C0 plus their call sites; the previous C189 confused the +0x38 byte getter with the setter.

## What would falsify it

a corrected vtable image or runtime call trace assigning any of those slots to a different operation
