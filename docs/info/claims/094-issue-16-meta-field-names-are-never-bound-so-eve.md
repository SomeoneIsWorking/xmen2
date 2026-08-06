---
id: C094
kind: claim
status: holds
created: 2026-08-06
tags: pc,recomp,native,ark,engine-init,rc-exe
---

## Claim

Issue #16: meta-field names are never bound, so every ARK field lookup compares class names

## Evidence

Measured, no decoding assumed. igMetaObject::getMetaField forwards to __internalNonRefCountedObjectList::searchMetas, which reads each candidate's name as *(char**)(field + [k_fieldName+8]). The global at libIGCore+0x15e418 is the exported DATA symbol igMetaField::k_fieldName and its +8 reads 0x0c, so the name lives at field+0x0c. The four fields registered on __internalObjectList's meta read, at that offset, 'igObject', 'igObject', 'igRefMetaField', 'igRefMetaField' -- CLASS names, not field names. The names the registration was handed ARE correct: the array at libIGCore+0x15b330 holds pointers to '_data' and '_count', correctly rebased to 0x2407a854 and 0x2407a110, with the offsets array holding 8 and 0x0c and the instantiate array holding two function pointers. So the two appended fields still carry the class name their instantiate function gave them, and igMetaObject::setMetaFieldBasicPropertiesAndValidateAll (0x10044a40) -- which ran, x5 -- never overwrote it. RULED OUT on the way, by reading the emitted C: the inlined strcmp inside searchMetas is NOT affected by issue #5's carry defect. It ends in the MSVC SBB EAX,EAX / SBB EAX,-1 sign idiom, and tools/recomp.py emits FLAG_C(C) for SBB, computes the preceding byte CMP at width 1 with FK_SUB, and FLAG_C masks by width -- so the comparison is faithful. That was the obvious suspect and it is innocent.

## What would falsify it

This assumes the four array entries are the meta fields rather than some other per-class list. The evidence is that setMetaFieldBasicPropertiesAndValidateAll indexes exactly this array ([[meta+0x28]+8])[i] to reach the field it configures. If those entries turn out to be something else, the whole reading collapses and the real field list is elsewhere.
