---
id: C038
kind: claim
status: holds
created: 2026-08-05
tags: 
reconfirmed: 2026-08-05
---

## Claim

A NATIVE LINUX EXECUTABLE of X-Men Legends II exists: 19MB, built from 1.5M lines of recompiled Xbox code. It runs, loads the XBE (5,726,208 bytes) and enters Xbox memory-layout initialisation before crashing in MapViewOfFileEx.

## Evidence

scratch/build-xbox/xml2_xbox_recomp links and runs. Output: 'X-Men Legends II (Xbox) - Static Recompilation / Loading XBE... / XBE loaded: 5726208 bytes / Initializing Xbox memory layout...' then SIGSEGV. gdb backtrace: MapViewOfFileEx <- xbox_MemoryLayoutInit <- WinMain. Reaching a build needed seven upstream fixes beyond the four earlier ones: EXCEPTION_POINTERS and a context struct given real shape (the opaque void* let a handler be REGISTERED but not written), GetCommandLineA/GetModuleHandleA declared and implemented over /proc/self/cmdline, and three tentative-definition clashes (g_icall_trace, g_icall_trace_idx, g_icall_count) that MSVC merges but GCC's -fno-common rejects. Plus the template placeholders: entry point 0x00225A09 and the game paths.

## What would falsify it

It crashes almost immediately and no game code has run. MapViewOfFileEx is implemented and faults dereferencing its mapping handle, so xbox_MemoryLayoutInit is passing an invalid one -- that is the next thing to debug, and nothing about the game booting or rendering is shown.

## Re-confirmed 2026-08-05

Narrowed but not fixed. Added a NULL check on obj_alloc in CreateFileMappingA (its result was written through unchecked -- a latent crash) and a guard in MapViewOfFileEx rejecting handles below 0x10000 before dereferencing. NEITHER GUARD FIRED, so the mapping handle is not NULL and not a Win32 pseudo-handle: it is a plausible-looking pointer that is nonetheless invalid when dereferenced. That rules out the two cheap explanations and leaves the object-table lifetime or a second MapViewOfFileEx call site (xbox_memory_layout.c:452, the mirror views) as where to look.
