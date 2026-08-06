---
id: 16
title: Native run: NULL dereference in __internalObjectList::arkRegisterInitialize
status: resolved
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

The list:

    list+0x00  0x240832c8   vtable
    list+0x04  0x00000001   igObject REFCOUNT -- not the count
    list+0x08  0x00a87500   array  -- matches edx
    list+0x0c  0x00000004   COUNT

**Corrected.** An earlier version of this note read `list+0x04` as the count and
concluded that only one field was registered. It is the refcount:
`instantiateAndAppendFields` does `MOV ECX,[ESI+0x4]; DEC ECX` on a field it has
just appended, which is a refcount decrement, and `igMetaObject::setMetaField
BasicPropertiesAndValidateAll` reads the array as `[[meta+0x28]+8]` indexed by
`i*4`. The array does hold four non-null entries -- 0x00a86c60, 0x00a86ca8,
0x00a87698, 0x00a876f0 -- which agrees with `+0xc` and not with `+0x04`.

So **four** meta fields are registered when `"_data"` is looked up, and the
lookup still fails. That is a different question from the one the earlier note
posed: not "why was nothing registered" but "why is `_data` not among these
four, or why does `searchMetas` not find it".

Nineteen classes look up `"_data"` in their own `arkRegisterInitialize`
(igStringRefList, igObjectList, igIGBFile, igIntList, ... ), so field
REGISTRATION is a separate step that must precede it -- and it DID run, in the
right order: the base `__internalNonRefCountedObjectList::arkRegisterInitialize`
is first entered at #1495 and the derived one at #1546.

## Next

1. Identify the four registered fields by NAME. Blocked on the `igMetaField`
   name offset, which `docs/RE/ark.md` does not record (it has `igMetaObject`'s
   offsets only). Field 0x00a86c60 has a heap string pointer at `+0x54`
   ("igOb..."), but that offset was found by poking and is not established.
   `setMetaFieldBasicPropertiesAndValidateAll` at 0x10044a40 is where the name
   is bound and is the function to read.
2. `X2_PEEK` can only read 1/2/4 bytes, so identifying a name means one run per
   guess. A string mode would turn this into a single run and is worth adding
   before the next attempt.
3. Then: is `_data` among the four (so `searchMetas` is at fault) or absent (so
   registration added the wrong set)?

## Earlier next (superseded)

1. Register dump and X2_PEEK at the fault to see which pointer is NULL
   (x86_diag_dump now fires on every stop path, including abort).
2. X2_ARGS on arkRegisterInitialize and its caller.
3. docs/RE/ark.md documents the ARK meta-object system and notes that
   igObject::constructDerived was never read -- this may be the point where that
   matters.

### Resolution (2026-08-06)
ROOT CAUSE was a translator defect four levels upstream, not an ARK problem (C095). tools/recomp.py modelled SBB's flags as a SUB of (b - c), which gets borrow-out wrong whenever a != b; MSVC's 'sbb eax,eax; sbb eax,-1' sign idiom then returned 0 (equal) instead of -1 (less) for any mismatch with a non-zero eax. libIGCore's string pool interns names via a binary search ending in that idiom, so it returned wrong entries: setString('igObject') and setString('_refCount') both gave 0x00a82b88. Meta-field names were therefore never bound -- all four fields on __internalObjectList's meta carried class names instead of '_data'/'_count' -- so getMetaField('_data') returned NULL and arkRegisterInitialize dereferenced it. FIXED by computing exact ADC/SBB flags at the instruction (x86_flags_adc/x86_flags_sbb, FK_EXPLICIT). Covered by tests/test_flags.c (22 known-answer checks), which was proven to FAIL against a reimplementation of the old model. VERIFIED: setString now returns distinct interned pointers; the fault is gone; pairs entered 1548 -> 1611; the run now advances into meta-field class registration and stops on an ordinary missing body at guest 0x100431b0, which native_discover.sh consumes. Ruled out earlier and worth keeping: searchMetas' own inlined strcmp is faithfully translated -- the defect was in SBB's flag OUTPUT, not in the compare.
