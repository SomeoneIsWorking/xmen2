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

Read from the body at libIGCore 0x10068da0: CALL [0x10077120] (LoadLibraryA); TEST ESI,ESI; JZ 0x10068e64 -> the warning path. On a non-NULL handle it calls GetProcAddress(h,"createLibraryObject") and on a NULL result takes the plain-object branch at 0x10068ddb. Confirmed at runtime: cg.dll and cgD3D8.dll both miss createLibraryObject and produce no warning, while msdia80.dll (which nothing in the game references -- no module in the install contains the string 'msdia') produced the warning purely because LoadLibraryA answered NULL. Mapping it INERT (real image, real export table, no recompiled bodies, imports unbound, DllMain not run) cleared the sweep and the run advanced to KERNEL32!SuspendThread in libCriMovie.

## What would falsify it

a run in which the engine CALLS into an inert module -- x86_dispatch aborts by name saying the module was never in the build. That would mean 'loadable but not runnable' is not enough for that DLL and it has to go through the lift pipeline.
