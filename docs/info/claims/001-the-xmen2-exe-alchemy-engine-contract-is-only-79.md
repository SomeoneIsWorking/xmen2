---
id: C001
kind: claim
status: holds
created: 2026-08-04
tags: 
---

## Claim

The XMen2.exe -> Alchemy engine contract is only ~794 named, MSVC-mangled C++ symbols, not the ~49k the DLLs export; so replacing the engine is a bounded spec, not an open-ended rewrite.

## Evidence

winedump -j import XMen2.exe: libIGSg 290, libIGCore 160, libIGAttrs 134, libIGMath 125, libIGGfx 61, libIGUtils 15, libIGDisplay 9 = 794. winedump -j export sum over libIG*.dll = 49357.

## What would falsify it

Finding that the exe reaches engine code by any path other than the PE import table (LoadLibrary+GetProcAddress, ordinal-only imports, or a delay-load table) would raise the real surface above 794.
