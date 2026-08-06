---
id: C102
kind: claim
status: falsified
created: 2026-08-06
tags: pc,recomp,discovery,tooling
falsified_on: 2026-08-06
---

## Claim

Most unidentified direct-call targets are INSIDE existing functions, so they need splits rather than seeds

## Evidence

tools/seed_code_imms.py now also collects direct CALL/JMP targets that are not function entries -- the translator already knows them, since that is exactly when it emits x86_call_unknown, so the running game should never discover them one at a time (it was: four rounds, one per round, all in XMen2.exe). Counted across the modules: XMen2.exe has 6672 such targets, libIGGfx 2680, libIGGui 58. But the split between the two kinds is the finding: of the exe's 6672, only 51 are NEW function starts -- 6627 land INSIDE an already-detected function. Those need a SPLIT, which seeding cannot perform, and issuing 6627 speculative splits would reshape the function database wholesale on the strength of a heuristic. So the seeder emits the 51 (and 74 for libIGGfx, 2 for libIGGui) and REPORTS the rest by count rather than acting on them; the discovery loop already escalates an individual address to a split when it actually blocks the run, which is the evidence-driven version of the same thing.

## What would falsify it

The 6627 are counted, not inspected. If a sample of them turns out to be ordinary intra-function branches that Ghidra folded correctly, the count is harmless noise and the report should say so; if they are genuinely separate functions, the boundary detector is under-splitting at scale and that is a much larger finding than this tool. Nobody has looked yet.

## FALSIFIED 2026-08-06

FALSE, and its own falsifier is what caught it -- it said nobody had looked at the 6627, and looking took one sample. Every candidate examined was a JMP from a function to an address INSIDE ITSELF, at a decoded instruction start, which recomp.py emits as a plain goto. They were never unidentified targets. The defect was in my counting condition, not in the data: I tested 'flow is not a known entry point' where recomp.py tests 'flow is not in THIS function's decoded addresses AND not a known entry point'. Dropping the same-function test turned every ordinary intra-function branch into a false candidate. Corrected counts, with the condition matching recomp.py exactly: XMen2.exe has 14 unresolvable direct targets (9 new function starts, 11 needing a split), libIGGfx 3, libIGGui 0, libIGCore 0 -- against 6672 and 6627 claimed. This is the 'a grep count is text, not code' trap in its exact form: a number was produced, believed, and written into a claim as a finding about the boundary detector, when it was a finding about my predicate. Superseded by C103.

> Anything that cited this claim as proof must be re-checked. Grep the repo for it.
