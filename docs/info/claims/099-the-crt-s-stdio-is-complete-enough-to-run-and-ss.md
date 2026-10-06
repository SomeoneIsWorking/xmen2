---
id: C099
kind: claim
status: holds
created: 2026-08-06
tags: pc,jit,native,host,crt,sse
---

## Claim

The CRT's stdio is complete enough to run, and SSE packed logic is translated on a real XMM register file

## Evidence

Three things, measured together: pairs entered 2314 -> 2819, battery 33/33, ctest 5/5. (1) stdio: fflush, fputc, fputs, fgetc, fgets, ungetc, fwrite, fprintf and vfprintf, the last two on the format walker. _iob is now REAL guest memory -- MSVCRT exports it as DATA and the guest reaches stderr as &_iob[2], so a FILE* here is either one of our small fopen handles or a pointer into that array, and the lookup accepts both; they cannot be confused because a handle is 1..64 and the array is on the guest heap. (2) A latent defect fixed on the way: crt.c's fopen passed the guest's path straight to the host, bypassing the win_path translation that CreateFileA has used all along -- so a path like 'Data\\foo.XMLB' could never open, and would have surfaced as a missing asset rather than as a failed open. (3) SSE: Gap::Core::igGetCPUCaps probes for SSE by executing ORPS XMM0,XMM0 under SEH, an identity by value that exists only to fault if the OS forbids SSE. Rather than special-case it as a no-op -- right for that operand pair and silently wrong for any other -- the CPU model gained a real 128-bit register file and ORPS/ANDPS/XORPS plus the PD spellings are translated properly, register and memory source. The probe is then a no-op because x|x == x, which is the same answer for the right reason. libIGCore's unsupported instructions drop from 7 in 4 functions to 6 in 3, all RCR.

## What would falsify it

The XMM file has no MOVUPS/MOVAPS, so nothing can yet LOAD or STORE those registers -- the packed-logic ops are reachable only on registers whose contents no translated instruction has set. That is sufficient for the probe and for nothing else, and a real SSE code path would produce garbage rather than a refusal. If MOVUPS is ever reached, the register file must be completed before any result from it is believed.
