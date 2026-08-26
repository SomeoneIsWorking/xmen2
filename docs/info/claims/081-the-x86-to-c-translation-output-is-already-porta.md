---
id: C081
kind: claim
status: holds
created: 2026-08-05
tags: pc,recomp,native,sdl,portability,macos,arm64
depends: src/native/guest_memory.c#guest_memory_init, src/recomp/x86rt.h#x86_guest_pointer
---

## Claim

The x86-to-C output and native runtime run on both x86-64 Linux and arm64 macOS; Apple Silicon uses a translated guest arena rather than an identity-mapped low 4 GB

## Evidence

Measured 2026-08-26: all twenty generated modules link into a native `Mach-O 64-bit executable arm64` with the normal 4 GB `__PAGEZERO`. `x86rt.h` translates memory operands through `g_guest_memory_base`, spells x86's unaligned memory operations without C alignment assumptions, implements the shipped SSE surface with NEON/portable scalar semantics, and supplies AArch64 CPUID/RDTSC answers for the translated x86 contract. The 93-check native battery includes mixed committed/decommitted 4 KiB Windows pages inside one 16 KiB Apple hardware page; the full 106-test CTest gate passes 103 with its three optional-data tests explicitly skipped. A driven D3D8 run clears the former post-`i105.sfd` allocator fault, enters the playable world, enables the world shadow pass and continues presenting geometry for another 50 seconds.

## What would falsify it

An arm64 fault caused by a raw logical guest address, a generated memory operand that bypasses the arena base, an instruction path whose AArch64 result differs from the x86 oracle, or failure of the native battery on either supported architecture.
