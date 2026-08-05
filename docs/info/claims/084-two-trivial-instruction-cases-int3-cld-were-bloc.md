---
id: C084
kind: claim
status: holds
created: 2026-08-05
tags: pc,recomp,translator,coverage
---

## Claim

Two trivial instruction cases (INT3, CLD) were blocking whole functions from translating. Adding them took libIGSg from 91.0% of functions and 68.7% of instructions to 99.9% of both.

## Evidence

Measured with tools/recomp.py report before and after. INT3 alone blocked 545 of libIGSg's 6118 functions, holding 54,582 instructions -- MSVC emits it as an unreachable trap after a call it proved never returns, so it sits INSIDE real bodies rather than only as inter-function padding. Refusing to translate the instruction discarded the whole function. It is now emitted as x86_int3(addr), which aborts naming the guest address if it ever executes. CLD asserts DF=0, which the runtime already assumes unconditionally; STD is deliberately still untranslated, so a module that actually sets DF still refuses rather than silently running string ops backwards. Whole-project coverage after: 8 modules, 99.3-100% of functions and 97.8-99.9% of instructions, 36,048 of 36,340 functions.

## What would falsify it

if a run ever reaches x86_int3, the assumption that these are unreachable traps is wrong for that site and it needs real semantics; and if any module is found to contain STD, the CLD-as-no-op shortcut is unsafe for it
