---
id: 12
title: The Ghidra project held a libIGSg.dll that is not the shipped one, and the exporter reused it silently
status: resolved
symptom: The analysis database disassembles different bytes from the authenticated file mapped at runtime: Ghidra reports 09 10 (OR [EAX],EDX) at the entry point where the shipped DLL has 55 8b ec 53 (a DllMain prologue). Section sizes disagree too -- Ghidra .text vsize 0x8ee00 vs the file's 0x6e240
tags: pc,ghidra,provenance,tooling
created: 2026-08-05
updated: 2026-08-14
---

## How it surfaced

libIGSg's PE entry point (rva 0x6f173) was not a function entry in the analysis
database. Defining it failed with "already inside a function"; splitting it
out succeeded, and the split function then started with `OR dword ptr
[EAX],EDX` -- which is not a function prologue. Reading the shipped file at
that offset gives
`55 8b ec 53 8b 5d 08 56 ...`: push ebp / mov ebp,esp / three arguments at
ebp+8, +0xc, +0x10. A textbook DllMain.

The file and the database disagree about the BYTES.

## Cause

The Ghidra project already contained libIGSg.dll when this work started -- it
was the ONLY module in it (the index listed exactly one program). The importer
skipped an image already present in the project, to avoid creating
libIGSg.dll_1 with a separate analysis. That guard reused an image of unknown
provenance instead of the one GAME_PC_DIR holds.

The section table settles it. The shipped file has .text vsize 0x6e240 and
.rdata at rva 0x70000; Ghidra's export reports a .text block of 0x8ee00, which
runs past where .rdata begins. Those are different images.

## Consequence

Every instruction-level conclusion exported from that database described a
binary the runtime was not mapping. libIGSg is 6118 functions, so this is not a
corner case -- and NOTHING about it was visible until an entry point happened
to land in the disagreeing region.

## Resolution

The exporter records each imported PE's SHA-256 and forces re-import when an
existing program's recorded hash differs from `GAME_PC_DIR`, or when an
existing program has no provenance stamp at all. “Already imported” is no
longer treated as “imported from this file.” The shipping predicate's selftest
drives the matching, mismatched, unknown-existing, and genuinely-new classes.

The current libIGSg stamp exactly matches the installed DLL. The independent
export verifier reports five agreeing sections, 4,714 functions, and zero
truncated bodies.
