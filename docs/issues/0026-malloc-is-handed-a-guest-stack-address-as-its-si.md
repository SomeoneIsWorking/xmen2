---
id: 26
title: malloc is handed a guest stack address as its size, on the code path that runs after a longjmp resume
status: resolved
symptom: malloc(1880094328 = 0x700ff678) -- that is not a size, it looks like an ADDRESS. Asked for by guest 0x0065e31d. The guest then takes its own out-of-memory path and dies calling an uninstalled handler at object+0x40 (issue #25).
tags: pc,native,rc-exe,setjmp,stack,memory
created: 2026-08-06
updated: 2026-08-06
---

## What is certain

Read from the exe, not inferred:

    0065b85d  PUSH EBX / PUSH ESI / PUSH EDI      ; prologue
    0065b866  PUSH ESI
    0065b86a  CALL 0x00672a08                     ; __cdecl, caller cleans
    0065b86f  PUSH 0x50                           ; <-- THE SIZE, 80 bytes
    0065b871  PUSH ESI
    0065b874  CALL 0x0065e314
    0065b879  ADD ESP,0xc                         ; cleans all three pushes

and the allocator itself is four instructions:

    0065e314  PUSH dword ptr [ESP + 0x8]          ; the size = 0x50
    0065e318  CALL malloc
    0065e31d  POP ECX
    0065e31e  RET

The exe is self-consistent: 0x50 is pushed, and `[ESP+8]` at the allocator's
entry is exactly that slot. The boundary ring agrees with the arithmetic --
0x0065b85d entered at esp 0x700ff584, 0x00672a08 entered at 0x700ff570 and
left at 0x700ff574 (a clean __cdecl +4), 0x0065e314 entered at 0x700ff568, so
`[ESP+8]` is 0x700ff570, which is the slot `PUSH 0x50` wrote.

**And the value that arrives at malloc is 0x700ff678.** That is a guest stack
address from a frame FURTHER UP, not 0x50.

## What is NOT established

Where the shift comes from. The one thing that makes this path new is the
setjmp/longjmp work: this code runs only after `crt: longjmp RESUMED into a
generated body`, and before that the run stopped at the longjmp itself. So the
resume is the first suspect -- but it has NOT been shown to be at fault, and
the arithmetic above says the stack was correct as recently as the entry to
0x0065e314.

Two readings, and they need different fixes:

1. **The resume restores a stack that is subtly wrong.** x86_setjmp_done
   restores the whole register file from the snapshot and pops the return
   address. That is right for the value of ESP, but nothing has checked that
   the guest stack CONTENTS above the resumed frame are what the exe expects,
   nor that the host-side state owned by the frames the longjmp destroyed (the
   ark scratch-stack pointer, for one) has been unwound.
2. **A mistranslation in one of the bodies between the resume and here**, which
   would have nothing to do with setjmp and simply never ran before.

## How it was found, which is the reusable part

Not by reading code. Two diagnostics, added in the commit that records this:

- `guest_malloc` now reports EXHAUSTION by name, with the request, the arena
  size, the live count and the high-water mark. It immediately refuted the
  obvious hypothesis -- the arena was 512 MB and the high-water was 127 KB, so
  the heap was never the problem.
- `malloc` now reports a size that is obviously an ADDRESS, naming the guest
  caller. That turned "the game died calling a garbage pointer" into "guest
  0x0065e31d asked for 0x700ff678 bytes" in one run.

Before them, this failure surfaced as issue #25: a crash calling an
uninitialised callback in a function with no visible connection to memory.

### Resolution (2026-08-06)
ROOT CAUSE: a translator defect, not the exe. recomp.py emitted PUSH of an ESP-relative operand as 'C->esp -= 4; WR32(C->esp, RD32(C->esp + 8))', which reads [esp+4] because the read uses the already-decremented ESP. Intel computes the address from the ORIGINAL ESP. POP had the mirror defect: its destination must be computed AFTER the increment.

So XMen2.exe 0x0065e314 -- four instructions, 'PUSH dword ptr [ESP+8]; CALL malloc' -- passed the slot next to the size. The guest stack dump at the failing call is what settled it: +04 (the value pushed) held a pointer while +16 held the 0x50 its caller had pushed.

Fixed in tools/recomp.py with three unit tests, every module re-emitted. Issues #24, #25 and #26 were all downstream of this one instruction.
