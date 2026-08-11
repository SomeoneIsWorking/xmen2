---
id: 46
title: A DLL that ships in the install but was never recompiled has to be LOADABLE, not NULL
status: resolved
symptom: x86_missing_import: USER32.dll!DrawTextA -- the Alchemy report box, this time because LoadLibraryA("msdia80.dll") returned NULL
tags: pc,native,kernel32,loader,libIGCore,root-cause
created: 2026-08-11
updated: 2026-08-11
---

## The stop

Same shape as issue #45 and a different cause. The engine's library loader
sweeps the game directory and calls `LoadLibraryA` on every DLL in it
(`igLibraryList::_instantiateFromPool` -> `Gap::Core::igWin32LibraryLoader::load`,
libIGCore `0x10068da0`). The argument watch names the file with no inference:

    [ARGS] -> 0x1004c0c0 igReportWarning
    [ARGS]      arg[1] -> "Library %s could not be loaded. Check its consistency
                           with the Alchemy dlls in use. ..."
    [ARGS]      arg[2] 0x00ac0048 -> "msdia80.dll"

19 loads, one call site, one warning -- and that warning goes to the modal
report box, which needs `USER32!DrawTextA`. DrawTextA is not the problem.

## What the loader actually requires

Read from the body at `0x10068da0`:

    CALL [0x10077120]        ; LoadLibraryA(name)
    TEST ESI,ESI / JZ 0x10068e64   ; NULL -> the warning path
    PUSH "createLibraryObject" / CALL [0x1007712c]   ; GetProcAddress
    TEST EAX,EAX / JZ ...    ; not exported -> a PLAIN library object

So a DLL that is not an Alchemy library is fine: `cg.dll` and `cgD3D8.dll` go
down exactly that branch and the engine is content. **The only branch that
warns is a NULL handle.**

## Root cause

`msdia80.dll` is Microsoft's Debug Interface Access DLL, shipped beside the
game. Nothing in the game references it -- no module in the install contains
the string "msdia" (checked with `strings` over all 18 DLLs and the exe; the
only DIA-adjacent hits are three unrelated `CoCreateInstance` import names).
It is loaded because it is in the directory, and for no other reason.

This host answered NULL for it, and NULL was the wrong answer: it means "there
is no such module", which is false -- the file is right there and it loads on
Windows.

## The fix: a third category of module

`kernel32.c` now has three answers instead of two:

| the module is | the handle is |
|---|---|
| recompiled and mapped | its image base (as before) |
| implemented natively by this host | a synthetic sysmod handle (as before) |
| a real PE in the install that was never lifted | **its image base, mapped INERT** |

An inert module is mapped by `pe_map` like any other, so the handle is a real
base and `GetProcAddress` reads the REAL export table -- `createLibraryObject`
still gets a truthful NULL, because msdia80 genuinely does not export it. What
is missing is stated at load and never hidden: no body was translated, the
imports are NOT bound, `DllMain` has NOT run. `X86Module.inert` carries the
distinction, and `x86_dispatch` prints a different report for a call into one
(naming the module and saying it was never in the build) so
`native_discover.sh` is not handed a seed for a module Ghidra has never
exported.

Eligibility is narrow on purpose: **the file must exist in the install
directory.** A system DLL this host does not provide is not there, so
`DSOUND.DLL` and `dpnhpast.dll` still get NULL -- which is right, because the
game handles those absences and would otherwise call into a handle it was
given.

Recompiling msdia80 was the other option and is the wrong one: 625 KB of
Microsoft COM code, nothing calls it, and lifting it would grow the build for
a module that must never be entered.

## Verified

The run gets past the sweep: msdia80 maps at 0x10370000, `createLibraryObject`
misses truthfully, `msvcr71.dll` takes the native-sysmod path, and the engine
continues into `CoCreateInstance` (refused truthfully, REGDB_E_CLASSNOTREG) and
then into libCriMovie, which stops at `KERNEL32!SuspendThread` -- the next
frontier, and part of the guest-thread subsystem (issues #42/#43), not this one.

`kernel32_inert_report` prints at exit in BOTH directions: the modules mapped
this way, or "no unrecompiled install DLL was mapped this run".
