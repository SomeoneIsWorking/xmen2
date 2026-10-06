---
id: C148
kind: claim
status: holds
created: 2026-08-11
tags: native,loader
---

## Claim

The engine's library sweep needs a DLL in the install to be LOADABLE, not runnable: igWin32LibraryLoader::load warns modally only when LoadLibraryA returns NULL, and tolerates a module that does not export createLibraryObject by giving it a plain library object

## Evidence

Read from the retail body at libIGCore 0x10068da0: `CALL [0x10077120]`
(`LoadLibraryA`); `TEST ESI,ESI`; `JZ 0x10068e64` reaches the warning path. On
a non-NULL handle it calls `GetProcAddress(h, "createLibraryObject")` and a
NULL export takes the plain-object branch at 0x10068ddb. Confirmed at runtime:
cg.dll and cgD3D8.dll both lack `createLibraryObject` and produce no warning,
while msdia80.dll produced the warning only when `LoadLibraryA` returned NULL.

## What would falsify it

a run in which the engine calls into an inert module and the JIT refuses because
the module has no executable image. That would mean "loadable but not
runnable" is insufficient for that DLL and its authenticated image must be
available to runtime translation.
