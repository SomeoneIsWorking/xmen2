---
id: 11
title: Calling a guest function from host code without pushing a return address walks ESP upward, silently
status: resolved
symptom: SIGSEGV reading just ABOVE the top of the guest stack after many host-initiated guest calls; looks like stack corruption rather than a missing push
tags: pc,recomp,native,abi,guest-stack
created: 2026-08-05
updated: 2026-08-05
---

## Symptom

x2native faulted at 0x30100000 -- 64 bytes above guest_stack_top -- while
running libIGCore's static constructors. A fault ABOVE a stack reads as
corruption; the cause was arithmetic.

## Cause

Every recompiled body is entered with its return address already on the guest
stack (each emitted call site pushes one) and its RET pops it. `_initterm`
dispatched each constructor with a bare `x86_dispatch`, pushing nothing, so
each of the 51 constructors popped 4 bytes nobody had pushed. ESP climbed 4
bytes per call until it left the mapped stack.

## Fix

`x86_guest_call(C, target)` in src/native/x86rt_native.c pushes the return
address the body will pop. Host code that enters guest code uses it; the
convention now lives in one place instead of being re-derived per call site.

## Why it was hard to see

The imbalance is invisible until ESP crosses a page boundary, so the failure
appears an arbitrary number of calls after the mistake and in a different
function. Nothing about the faulting body is wrong.
