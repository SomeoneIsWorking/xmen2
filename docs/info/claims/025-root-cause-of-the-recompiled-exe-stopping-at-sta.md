---
id: C025
kind: claim
status: holds
created: 2026-08-05
tags: 
---

## Claim

ROOT CAUSE of the recompiled exe stopping at startup: the recompiler models RET as a C , which is wrong for functions that deliberately alter their own return address. MSVC's __SEH_prolog is exactly such a function, and it is the second function the exe executes.

## Evidence

Call-history ring (X86_TRACE_CALLS) shows: entry(0x006725f4) -> __SEH_prolog(0x00672904) -> indirect call to 0x002d28a8. Disassembly of __SEH_prolog shows it pushes a handler, reads the caller's frame-size argument from [ESP+0x10], overwrites that slot with EBP, does SUB ESP,EAX to allocate the frame, builds the SEH record, and then RETs -- by which point ESP points at a value it constructed, not at the address it was called with. Emitting  discards that value, so control resumes in the wrong place and the next indirect call reads garbage (0x002d28a8 is in .rdata, not code, so it is not a lost-base pointer -- checked).

## What would falsify it

The fix is to make RET honour the popped value: pass each recompiled function its expected return address and, when the popped value differs, tail-dispatch to the popped target instead of returning. Until that is implemented and the exe gets past __SEH_prolog, ANY claim about the recompiled exe running game code is unfounded. Note this affects every function using SEH -- 825 of them touch FS -- so it is not a one-off.
