---
id: C082
kind: claim
status: holds
created: 2026-08-05
tags: pc,recomp,native,pe,relocation
---

## Claim

A recompiled module that cannot get its preferred base MUST have its .reloc fixups applied. The recompiled CODE is base-independent, so the omission is invisible until the module's own DATA pointers are dereferenced.

## Evidence

Measured 2026-08-05. Every libIG*.dll is linked for 0x10000000, so with two modules linked one must move; libIGDisplay moved to 0x20000000 and pe_map applied 2460 HIGHLOW fixups. The reason the gap is silent: emitted code resolves absolute references as G_IMGBASE + offset, which is correct at any base, so nothing about code execution reveals the problem -- but vtables, string tables and jump tables baked into .data still hold preferred-base addresses. Also measured: a non-PIE host binary sits at 0x400000 and steals XMen2.exe's preferred base, forcing a needless relocation of the module least able to tolerate one; x2native is built -pie for that reason.

## What would falsify it

if a module ever has to move and has no relocation directory, pe_map refuses -- if that refusal ever fires, this claim's remedy is unavailable for that module and the identity-mapping design has to be revisited (issue #10's base-offset alternative)
