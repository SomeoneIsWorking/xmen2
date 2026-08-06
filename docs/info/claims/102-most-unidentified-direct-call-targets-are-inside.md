---
id: C102
kind: claim
status: holds
created: 2026-08-06
tags: pc,recomp,discovery,tooling
---

## Claim

Most unidentified direct-call targets are INSIDE existing functions, so they need splits rather than seeds

## Evidence

tools/seed_code_imms.py now also collects direct CALL/JMP targets that are not function entries -- the translator already knows them, since that is exactly when it emits x86_call_unknown, so the running game should never discover them one at a time (it was: four rounds, one per round, all in XMen2.exe). Counted across the modules: XMen2.exe has 6672 such targets, libIGGfx 2680, libIGGui 58. But the split between the two kinds is the finding: of the exe's 6672, only 51 are NEW function starts -- 6627 land INSIDE an already-detected function. Those need a SPLIT, which seeding cannot perform, and issuing 6627 speculative splits would reshape the function database wholesale on the strength of a heuristic. So the seeder emits the 51 (and 74 for libIGGfx, 2 for libIGGui) and REPORTS the rest by count rather than acting on them; the discovery loop already escalates an individual address to a split when it actually blocks the run, which is the evidence-driven version of the same thing.

## What would falsify it

The 6627 are counted, not inspected. If a sample of them turns out to be ordinary intra-function branches that Ghidra folded correctly, the count is harmless noise and the report should say so; if they are genuinely separate functions, the boundary detector is under-splitting at scale and that is a much larger finding than this tool. Nobody has looked yet.
