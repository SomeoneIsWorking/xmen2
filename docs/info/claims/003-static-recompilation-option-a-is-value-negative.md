---
id: C003
kind: claim
status: falsified
created: 2026-08-04
tags: 
falsified_on: 2026-08-04
---

## Claim

Static recompilation (option A) is value-negative for this title because both shipped builds are already x86: the PC build is x86-32 PE and the Xbox build is x86-32 XBE. Recomp's payoff on N64/PS2 ports is escaping an alien ISA, which does not apply here.

## Evidence

PC: XMen2.exe + 16 libIG*.dll are i386 PE (winedump parses them as such; i686 import/export tables). Xbox: default.xbe is x86. Wine 11.0-staging already executes the PC binaries natively on this machine.

## What would falsify it

Discovering the PC build ships a non-x86 code payload (a bytecode VM with its own ISA, or PPC/MIPS blobs) that dominates the gameplay logic would restore a recomp rationale.

## FALSIFIED 2026-08-04

Too strong. It framed recomp's only payoff as escaping an alien ISA, which made 'both builds are x86' look decisive. The payoff it ignored is OWNERSHIP: static recomp yields a native, buildable, portable codebase in which any function can be replaced with hand-written C incrementally, with a working game from day one. That directly answers the objection that a clean reimplementation is person-years of work before anything boots. The x86-specific DIFFICULTY argument in C003 (variable-length instructions, undecidable code/data separation, pervasive vtable dispatch) survives and still applies -- but difficulty is not the same as value-negative.

> Anything that cited this claim as proof must be re-checked. Grep the repo for it.
