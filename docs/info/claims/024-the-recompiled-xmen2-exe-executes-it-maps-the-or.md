---
id: C024
kind: claim
status: holds
created: 2026-08-05
tags: 
---

## Claim

The recompiled XMen2.exe EXECUTES: it maps the original image at 0x00400000, resolves all 989 imports, enters the recompiled entry point and runs real recompiled instructions through the CRT startup.

## Evidence

src/app/x2run.c. Output: 'guest image written into the runner's own reserved range at 0x00400000 (6767814 bytes), 6 sections' / 'imports resolved' / 'entering recompiled XMen2.exe at 0x006725f4', then execution proceeds until x86_dispatch reports an indirect call to 0x002d28a8 with no recompiled body. Three obstacles were solved to get here: XMen2.exe has NO relocation directory so it cannot be rebased and must sit exactly at 0x00400000; Wine's own file mappings occupy that range before user code runs, so neither VirtualAlloc nor an early constructor nor WINEPRELOADRESERVE can claim it; the fix is to link the RUNNER at image base 0x00400000 with its own sections placed above the guest image, which makes Wine reserve the whole range as our image, after which the low part is VirtualProtect-ed and written into. Ordinal imports (@N from WS2_32/OLEAUT32) needed GetProcAddress with the ordinal cast to a pointer.

## What would falsify it

It runs but does NOT get far: the first indirect call to an untranslated target stops it, and 0x002d28a8 is below the image base, which suggests either an uninitialised function pointer or a defect in how the image is laid out -- NOT diagnosed. Nothing about the game booting is demonstrated. No frame, no window, no engine initialisation.
