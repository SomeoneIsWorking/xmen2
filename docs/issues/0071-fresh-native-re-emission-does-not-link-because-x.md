---
id: 71
title: Fresh native re-emission does not link because x86_tail_dispatch exists only in the hosted runtime
status: resolved
symptom: After adding an isolated native override and re-emitting XMen2, x2native fails to link with undefined reference to x86_tail_dispatch from many generated functions
tags: pc,native,recomp,runtime,tail-call,build
created: 2026-08-14
updated: 2026-08-14
---

## Root cause

The shared emitter had begun translating indirect tail jumps through `TAIL_DISPATCH` as part of the depth-aware hosted fix (C181), but `src/native/x86rt_native.c` still implemented only the older one-shot `x86_dispatch`. Existing native generated chunks predated that emitter change, so the missing runtime contract stayed hidden until a legitimate re-emission.

## Fix

Port the proven depth-aware dispatcher contract into the native runtime: a same-frame tail target is iterated by the current dispatcher, while a tail jump reached through a direct generated C call opens a nested dispatcher and finishes before the direct caller resumes. The generated body is retained unchanged.

## Evidence

Before: link fails from `XMen2_011.c` and other chunks with undefined `x86_tail_dispatch`. After: all 22 XMen2 chunks link; a default `./run.sh` capped run maps 16,453 exe bodies, reaches 18 presents / 36 draws / 0 refused, and exits only at `X2_MAX_FRAMES=10`. The same run loads the derived prompt font through `X2_ASSETS`.

## Falsifier

A native path in which nested generated tail dispatch resumes its direct caller before the target returns, or a stack-balance regression on the full smoke loop, would show that copying the hosted depth contract was insufficient.
