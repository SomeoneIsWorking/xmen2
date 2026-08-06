---
id: 16
title: Native run: NULL dereference in __internalObjectList::arkRegisterInitialize
status: open
symptom: SIGSEGV at (nil) in Gap::Core::__internalObjectList::arkRegisterInitialize (libIGCore 0x1001a660), reached via ARK registration after igArkRegister now dispatches correctly. Ring shows igArenaMemoryPool::contains / getMemorySize / retrieveVTablePointer just before
tags: pc,recomp,native,ark,engine-init,rc-exe
created: 2026-08-06
updated: 2026-08-06
---

## Where it stops

The frontier after C092 (image bounds) and the esp-relative indirect-call fix.
The run now clears the whole arena allocator path and the ARK register thunk, and
reaches 1548 distinct (entry point, module) pairs, up from 1398.

    addr2line -> fn_libIGCore_1001a660  =  __internalObjectList::arkRegisterInitialize

## Next

1. Register dump and X2_PEEK at the fault to see which pointer is NULL
   (x86_diag_dump now fires on every stop path, including abort).
2. X2_ARGS on arkRegisterInitialize and its caller.
3. docs/RE/ark.md documents the ARK meta-object system and notes that
   igObject::constructDerived was never read -- this may be the point where that
   matters.
