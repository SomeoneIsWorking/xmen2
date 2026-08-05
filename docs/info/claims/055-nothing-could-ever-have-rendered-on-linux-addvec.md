---
id: C055
kind: claim
status: holds
created: 2026-08-05
tags: xbox
depends: patches/xboxrecomp/0004-linux-veh-and-nv2a.patch
---

## Claim

Nothing could ever have rendered on Linux: AddVectoredExceptionHandler was a no-op stub returning NULL, and the NV2A MMIO instruction decoder was inside #if defined(_WIN32). Both are now implemented, so GPU register faults reach the emulator.

## Evidence

win32_compat.c:1469 was 'return NULL; /* TODO: wire to sigaction */', so every handler installed on Linux was discarded -- proven by the crash reporter never printing on a SIGSEGV. It is now built on sigaction(SIGSEGV/SIGBUS, SA_SIGINFO|SA_ONSTACK) translating siginfo/ucontext into EXCEPTION_POINTERS and copying an edited context back on EXCEPTION_CONTINUE_EXECUTION, which is what lets the MMIO decoder advance RIP and write a destination register. XR_CONTEXT gained R8-R15 (the decoder indexes them) and the two duplicate CONTEXT structs were collapsed into one. Verified: the run now prints '[NV2A] Standalone GPU initialized: VRAM=64MB RAMIN=1024KB', '[NV2A] MMIO hook initialized', and on the next fault a full '[CRASH] Access violation ... Xbox VA of fault' report that had never appeared before.

## What would falsify it

no GPU register fault has been decoded yet -- the title dies in the CRT heap first. If nv2a_hook_handle_mmio turns out unable to decode this title's actual access patterns, the '[NV2A] could NOT decode' counter in main.c will say so.
