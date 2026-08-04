---
id: C003
kind: claim
status: holds
created: 2026-08-04
tags: 
---

## Claim

Static recompilation (option A) is value-negative for this title because both shipped builds are already x86: the PC build is x86-32 PE and the Xbox build is x86-32 XBE. Recomp's payoff on N64/PS2 ports is escaping an alien ISA, which does not apply here.

## Evidence

PC: XMen2.exe + 16 libIG*.dll are i386 PE (winedump parses them as such; i686 import/export tables). Xbox: default.xbe is x86. Wine 11.0-staging already executes the PC binaries natively on this machine.

## What would falsify it

Discovering the PC build ships a non-x86 code payload (a bytecode VM with its own ISA, or PPC/MIPS blobs) that dominates the gameplay logic would restore a recomp rationale.
