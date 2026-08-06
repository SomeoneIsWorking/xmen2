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

## Localised (2026-08-06)

Fault is exactly guest `0x1001a682 MOV EAX,dword ptr [ESI]` with `esi = 0`,
confirmed by the register dump (I024):

    eax ffffffff  ecx 00000004  edx 00a87500  ebx ffffffff
    esp 700ffd78  ebp 00a88238  esi 00000000  edi 00a87498

`esi` is the return value of `igMetaObject::getMetaField(this, "_data")` called
two instructions earlier, so the meta-field lookup returned NULL.

Everything that lookup depends on is present and correctly relocated (X2_PEEK):

    libIGCore+0x15f1ac   0x00a87498   the meta object -- matches edi
    metaObj+0x00         0x24084618   its vtable, correctly rebased
    metaObj+0x28         0x00a86ba0   the meta-field list
    libIGCore+0x15e418   0x00a87928   the global searchMetas is passed
    libIGCore+0x7a854    "_data"      the name being looked up

`getMetaField` is a 7-instruction forwarder to
`__internalNonRefCountedObjectList::searchMetas(list, [0x1015e418], name)`.

The list has **exactly one** entry:

    list+0x00  0x240832c8   vtable
    list+0x04  0x00000001   count
    list+0x08  0x00a87500   array  -- matches edx
    list+0x0c  0x00000004   capacity

So one meta field is registered on this meta object at the moment `"_data"` is
looked up, and it is not the one wanted. The single entry is at 0x00a86c60; its
name is not a plain `char *` in its first 18 words, so it is presumably an
interned string-pool reference and identifying it needs the `igMetaField`
layout, which `docs/RE/ark.md` does not record.

Nineteen classes look up `"_data"` in their own `arkRegisterInitialize`
(igStringRefList, igObjectList, igIGBFile, igIntList, ... ), so field
REGISTRATION is a separate step that must precede it. For
`__internalObjectList` that step did not add `"_data"`.

## Next

1. Find `__internalObjectList`'s field-registration function (the counterpart to
   `arkRegisterInitialize`), check with the reached set whether it ran, and with
   `X2_ARGS` what it registered. That separates "registration never ran" from
   "registration ran and added the wrong thing".
2. If it ran, the `igMetaField` name layout has to be read to identify the one
   entry that IS present -- and that belongs in `docs/RE/ark.md`, which
   currently documents `igMetaObject` offsets but not `igMetaField`'s.

## Earlier next (superseded)

1. Register dump and X2_PEEK at the fault to see which pointer is NULL
   (x86_diag_dump now fires on every stop path, including abort).
2. X2_ARGS on arkRegisterInitialize and its caller.
3. docs/RE/ark.md documents the ARK meta-object system and notes that
   igObject::constructDerived was never read -- this may be the point where that
   matters.
