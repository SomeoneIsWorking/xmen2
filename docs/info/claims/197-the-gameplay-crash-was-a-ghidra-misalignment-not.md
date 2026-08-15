---
id: C197
kind: claim
status: holds
created: 2026-08-15
tags: crash,recomp,ghidra
---

## Claim

The gameplay crash was a Ghidra misalignment, not a runtime bug: 0x00424240 (a vtable-reached function) exported as a 1-byte stub with zero instructions, and the recompiler's trap for it fired on the first virtual call in gameplay

## Evidence

scratch/logs/interactive.log carries 'x86_untranslated: reached guest 0x00424240 FUN_00424240 -- blocked by: no decoded instructions' right after the tutorial conversation ends. The bytes at 0x424240 decode as 'sub esp,0x14; push esi; mov esi,ecx' and the address is held by a .rdata vtable slot at 0x00682e40 whose neighbours are all genuine function entries; the export had FUN_00424242 (102 ins) starting two bytes in, reading the prologue tail as 'ADC AL,0x56'. After tools/ghidra_export.sh XMen2 --repair-stubs ALL: 0 of 16450 functions lack an instruction at their entry, the emitter reports 0 untranslated (was 4), and a 6200-frame gameplay run exits 0 with zero x86_untranslated hits (scratch/logs/postfix.log).

## What would falsify it

a gameplay run that traps on any x86_untranslated address, or a report of the same illegal-instruction crash on a build made after this repair
