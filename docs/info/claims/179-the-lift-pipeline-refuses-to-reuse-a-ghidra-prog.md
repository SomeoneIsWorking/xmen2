---
id: C179
kind: claim
status: holds
created: 2026-08-14
tags: pc,recomp,ghidra,provenance
---

## Claim

The lift pipeline refuses to reuse a Ghidra program whose source binary hash differs or whose provenance is unrecorded

## Evidence

tools/ghidra_export.sh provenance_needs_reimport is used by the shipping import decision and its lift_step_guard selftest exercises matching, mismatched, unknown-existing, and new-program classes; current libIGSg stamp equals the installed DLL SHA-256 and verify_export reports 5 sections, 4714 functions, 0 truncated

## What would falsify it

an existing project program is exported when its recorded SHA-256 differs from GAME_PC_DIR, or an existing unstamped program is reused without import
