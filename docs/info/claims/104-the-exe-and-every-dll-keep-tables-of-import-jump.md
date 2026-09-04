---
id: C104
kind: claim
status: holds
created: 2026-08-06
tags: pc,pe,imports
---

## Claim

The executable and every engine DLL contain six-byte import jump thunks that
remain executable guest entry points even when a disassembler does not define
them as functions.

## Evidence

MSVC emits a six-byte indirect jump per imported function. A PE scan counted
1,408 such entries: 208 in XMen2.exe, 240 in libIGGfx, 172 in libIGOpt, 153 in
libIGSg, 152 in libIGGui, 141 in libIGMath, 85 in libIGAttrs, 79 in
libIGUtils, 58 in libIGDisplay, 36 in libCriMovie, 30 in libIGLua, 29 in
libIGCore, and 25 in libMovie. XMen2.exe's table begins at 0x00643f00. These
bytes are ordinary runtime-translated guest code; PE import binding supplies
their indirect destinations.

## What would falsify it

A fresh PE scan finding a different count or an entry that is not a six-byte
import jump would invalidate the inventory. A runtime branch into one of these
entries that is not translated or does not reach the bound import would
invalidate the execution conclusion.
