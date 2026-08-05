---
id: 12
title: The Ghidra project held a libIGSg.dll that is not the shipped one, and the exporter reused it silently
status: investigating
symptom: Recompiled code disassembles differently from the file being mapped at runtime: Ghidra reports 09 10 (OR [EAX],EDX) at the entry point where the shipped DLL has 55 8b ec 53 (a DllMain prologue). Section sizes disagree too -- Ghidra .text vsize 0x8ee00 vs the file's 0x6e240
tags: pc,recomp,ghidra,provenance,tooling
created: 2026-08-05
updated: 2026-08-05
---

## How it surfaced

libIGSg's PE entry point (rva 0x6f173) had no recompiled body. Seeding it
failed with "already inside a function"; splitting it out succeeded, and the
split function then started with `OR dword ptr [EAX],EDX` -- which is not a
function prologue. Reading the shipped file at that offset gives
`55 8b ec 53 8b 5d 08 56 ...`: push ebp / mov ebp,esp / three arguments at
ebp+8, +0xc, +0x10. A textbook DllMain.

The file and the database disagree about the BYTES.

## Cause

The Ghidra project at scratch/ghidra already contained libIGSg.dll when this
work started -- it was the ONLY module in it (the index listed exactly one
program). tools/ghidra_export.sh deliberately skips import when a binary is
already in the project, to avoid creating libIGSg.dll_1 with a separate
analysis. That guard is right in general and wrong here: it made the exporter
reuse an image of unknown provenance instead of the one GAME_PC_DIR holds.

The section table settles it. The shipped file has .text vsize 0x6e240 and
.rdata at rva 0x70000; Ghidra's export reports a .text block of 0x8ee00, which
runs past where .rdata begins. Those are different images.

## Consequence

Everything recompiled from that JSON describes a binary we are not running.
libIGSg is 6118 functions, so this is not a corner case -- and NOTHING about it
was visible until an entry point happened to land in the disagreeing region.

## Fix in progress

Re-import with `tools/ghidra_export.sh libIGSg --reanalyze`.

## What this says about the tooling

The exporter must not trust a program that is already in the project. It should
record the file's hash at import and refuse to export when the hash of
GAME_PC_DIR's copy differs -- "already imported" is not the same as "imported
from this file". Until that exists, treat any module already present in a
project as unverified provenance.
