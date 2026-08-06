---
id: C103
kind: claim
status: holds
created: 2026-08-06
tags: pc,recomp,discovery,tooling
---

## Claim

Unresolvable direct-call targets are a handful per module, not thousands

## Evidence

Measured with the condition that matches recomp.py's emission of x86_call_unknown exactly -- a direct CALL/JMP whose target is neither a known function entry NOR an instruction inside the calling function, since the latter is emitted as a goto. XMen2.exe: 14 such targets, of which 9 are new function starts and 11 fall inside an existing function (needing a split). libIGGfx: 3. libIGGui and libIGCore: 0. Verified by sampling the rejected class first: eight candidates drawn at random from what the earlier, wrong predicate had flagged were all intra-function branches to decoded instruction starts, confirmed against each function's own instruction list. So the boundary detector is NOT under-splitting at scale, which is what the wrong count appeared to show.

## What would falsify it

The 11 split candidates in the exe have not been individually examined -- they are the residue after the intra-function class was removed, so they are genuinely unresolvable, but whether each is a real function start or a landing site Ghidra folded for a good reason is unknown. Eleven is small enough to look at one by one if any of them ever blocks a run.
