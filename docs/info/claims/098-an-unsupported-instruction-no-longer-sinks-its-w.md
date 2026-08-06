---
id: C098
kind: claim
status: holds
created: 2026-08-06
tags: pc,recomp,translator,coverage
---

## Claim

An unsupported instruction no longer sinks its whole function, and computed jumps resolve inside it

## Evidence

Two translator changes, both prompted by igGetCPUCaps -- a 898-instruction, 59-case CPU query that the run could not get past. (1) An instruction the translator cannot handle is now emitted in place as x86_unsupported_insn(ep, addr, name, reason), which aborts by name IF EXECUTED, instead of refusing the whole body. The design rule is unchanged -- an unhandled instruction must fail loudly, never become a no-op -- but refusing the function enforced it far more broadly than the rule requires. It is sound because the replacement never returns: an instruction that is not executed cannot affect state, and one that is executed does not continue. igGetCPUCaps uses SSE (ORPS) in ONE of its 59 cases and the engine only ever asks for cases 0 and 1, so the old behaviour stopped the run on a case it never executes. MEASURED: libIGCore goes from 5964 of 5968 functions to 5968 of 5968, with 7 unsupported instructions in 4 functions; XMen2.exe emits 14890 of 14893 with 3297 unsupported instructions in 42. (2) A function containing an indirect JMP now gets a label on every instruction and one dispatcher at the end that resolves the target locally, falling back to the global dispatcher for a genuine tail call. This is what a switch compiles to, and dispatching it globally could only ever fail: the run stopped on 'no recompiled body at 0x1006790e', which is jump-table entry [0] of the function it was already inside. 1299 functions across the ten modules contain one, 72k instructions, so nothing else pays for it. VERIFIED: the run now clears igGetCPUCaps entirely; distinct (entry point, module) pairs entered 2121 -> 2314; battery 33/33; ctest 5/5.

## What would falsify it

The local dispatcher switches on (target - G_IMGBASE), which is correct only while every in-function jump-table entry points into the SAME module. A table whose entries point into another module would underflow to the default and fall through to the global dispatcher -- correct by accident rather than by design, and it would be worth an explicit check if such a table is ever observed.
