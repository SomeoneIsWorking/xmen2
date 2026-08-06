---
id: 31
title: A killed build leaves a 0-byte .o that the build system treats as up to date
status: resolved
symptom: undefined reference to fn_libIGCore_100508b0 -- collect2: error: ld returned 1 exit status, for a symbol that IS defined in src/recomp/libIGCore_003.c
tags: build,tooling,rc-native
created: 2026-08-06
updated: 2026-08-06
---

## Symptom

A link that had worked minutes earlier fails with undefined references to generated bodies that are plainly present in the generated sources:

    src/recomp/libIGCore_002.c: undefined reference to `fn_libIGCore_100508b0'

`grep` finds `void fn_libIGCore_100508b0(CPU *C) {` in `src/recomp/libIGCore_003.c`, so the source is not the problem.

## Cause

The compile of `libIGCore_003.c` was KILLED partway through (a command timeout on a 2-minute limit; the native build of the generated sources takes longer than that). `cc` left a **0-byte** `libIGCore_003.c.o` behind, newer than its source, so make/ninja considered it up to date and linked it -- an object file with nothing in it.

Nothing is wrong with the recompiler output. The tell is the object size, not the compiler output.

## Fix

    find scratch/build-native -name '*.o' -size 0 -print -delete

then rebuild. Give the native build a real timeout: a from-scratch `x2native` link is several minutes, and killing it is what creates this.
