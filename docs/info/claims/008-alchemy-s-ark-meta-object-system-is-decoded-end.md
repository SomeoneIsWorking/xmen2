---
id: C008
kind: claim
status: holds
created: 2026-08-04
tags: 
---

## Claim

Alchemy's ARK meta-object system is decoded end to end: class registration is one libIGCore call (igArkRegister, 11 args), construction is delegated to igMetaObject::createInstance, and an abstract class redirects to its platform implementation through igMetaObject+0x3c which createInstance follows in a loop.

## Evidence

Ghidra decompile of libIGDisplay.dll (igWindow / igControllerManager / igWin32Window arkRegister/arkRegisterInternal/arkRegisterInitialize/_instantiateFromPool/getClassTypeLazy) and libIGCore.dll (igMetaObject::createInstance @0x10044380, both igArkRegister overloads @0x10045140/@0x10045180). Both igArkRegister signatures recovered from the MSVC mangling itself, not guessed. Written up in docs/RE/ark.md.

## What would falsify it

Building a class that registers via these calls and having libIGCore reject it, or having createInstance return NULL for it, would show the decode is incomplete. Nothing has yet been CONSTRUCTED this way -- the mechanism is read, not exercised.
