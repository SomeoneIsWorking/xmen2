---
id: C140
kind: claim
status: holds
created: 2026-08-07
tags: recomp,x87,native,timing,rc-exe
---

## Claim

tools/recomp.py translated EVERY integer x87 memory operand at 32 bits, so FILD qword read the low dword and sign-extended it -- 326 instructions across 12 modules. That is what stopped the native run: the engine's 64-bit nanosecond clock wrapped negative at 2^31 ns = 2.147 s, and XMen2.exe's frame limiter at 0x00401ff0 spins while (now - last) < 1/60 with no guard for a clock that went backwards.

## Evidence

The ring, once body entries recorded their return address, named the caller as 0x00401ffd -- the limiter's own CALL [EDX+0x28] site. The app object at 0x006f3ac4 froze at +0x1c = 2.1349 s and +0x04 = 2.1421 s in TWO runs whose frame counts differed by 10x (1051 vs 90), i.e. a fixed value, not a timing accident; 2.147483648 s is 2^31 ns. The engine side was measured innocent: getTimeAsLong returns a correct advancing int64 (-2541254811 then 3464323991) and MSVCRT _ftol returns the full 64 bits in EDX:EAX. After emitting RDI64 for the qword form and re-emitting every module, the same run goes past the limiter into scene traversal (igGraphPath, igCamera::activate) and stops on an ordinary missing body at 0x00589090.

## What would falsify it

a run of the fixed build that stops presenting again with the ring pointing at 0x00401ffd, or a module whose re-emitted body still contains RDI32 for a qword FILD (grep the generated chunks)
