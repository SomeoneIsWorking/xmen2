---
id: C082
kind: claim
status: holds
created: 2026-08-05
tags: pc,jit,native,pe,relocation
---

## Claim

A PE module that cannot get its preferred base must have its `.reloc` fixups
applied. Runtime translation is based on the mapped instruction address, so a
relocation omission may remain invisible until the module's own data pointers
are dereferenced.

## Evidence

Measured 2026-08-05. Every libIG*.dll is linked for 0x10000000, so with two
modules linked one must move; libIGDisplay moved to 0x20000000 and pe_map
applied 2460 HIGHLOW fixups. The JIT decodes the bytes at their mapped
addresses, so execution alone may not expose a missing data relocation, but
vtables, string tables, and jump tables in `.data` still contain preferred-base
addresses until the loader applies their fixups. Also measured: a non-PIE host
binary sits at 0x400000 and steals XMen2.exe's preferred base, forcing a
needless relocation of the module least able to tolerate one; x2native is
built PIE for that reason.

## What would falsify it

If a module ever has to move and has no relocation directory, pe_map refuses.
If that refusal fires, this remedy is unavailable for that module and the
base-offset address-space design must own the case.
