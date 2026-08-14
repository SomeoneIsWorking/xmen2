---
id: C192
kind: claim
status: holds
created: 2026-08-14
tags: input,xbox
---

## Claim

The Xbox path from gameplay to a controller physical value is resolved end to end: singleton accessor sub_00160E60 returns the input manager (object 0x0061d060, ctor sub_0015F680, vtable 0x004A9DAC with its RTTI word at 0x004A9DA8); manager slot +0x4c is sub_0015F940, which maps a player index through [this+idx*4+4] and returns the per-player controller at [this + n*0x388 + 0x3c]; controller slot +0x10 is sub_0015F5B0, the physical-float getter (C189). Cross-check: manager slot +0x24 is sub_0015FD10, and the analog-byte expander sub_00163E40 calls exactly that slot -- the same function that copies the 30 floats into the controller. Following the register chain from the 356 getController sites attributes 246 virtual calls to the returned controller. Every LITERAL physical index read through +0x10 is one of 0, 1, 2, 0xb, 0xc, 0x10, 0x11 -- never 8 or 9. The only non-literal readers are two sites in sub_001E4210, a virtual method (slot 0x2c of the vtable at 0x004B2600) whose index is its own first parameter.

## Evidence

tools/xbe_query.py chain 0x4c --slot 0x10 (356 accessor sites, 246 attributed calls); vtable 0x004A9D60 --count 64 showing both tables and the RTTI words 0x0052AE54/0x0052AE9C that separate them; func 0x15F940 / 0x15FD10 / 0x163E40 / 0x1E4210; find 0x001E4210 (one occurrence, in .rdata at 0x004B262C). xbe_query.py selftest passes 11 checks including a chain positive and a chain negative control.

## What would falsify it

a re-disassembly in which the register chain reaches a literal 8 or 9 at controller slot +0x10, or a caller of sub_001E4210 passing 8 or 9
